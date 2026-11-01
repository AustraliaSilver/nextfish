

# AAW-X: Extended Adaptive Aspiration Windows Architecture

---

## I. Phân Tích Điểm Yếu Còn Lại Của AAW Gốc

### 1.1 Bảy Lỗ Hổng Cần Giải Quyết

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    AAW REMAINING WEAKNESSES                              │
│                                                                          │
│  ❶ POINT ESTIMATE FALLACY                                               │
│     AAW dùng prev_score làm center → nhưng prev_score là point          │
│     estimate, không phải distribution. Không capture uncertainty         │
│     shape (skew, multimodal, heavy tails).                              │
│                                                                          │
│  ❷ REACTIVE EXPANSION                                                   │
│     Expand SAU KHI fail → tốn ít nhất 1 full search trước khi           │
│     correct. Nếu PREDICT fail trước → tránh hoàn toàn.                  │
│                                                                          │
│  ❸ SERIAL WINDOW SEARCH                                                 │
│     Chỉ search 1 window tại 1 thời điểm. Parallel hardware             │
│     (multi-core) không được exploit cho aspiration.                     │
│                                                                          │
│  ❹ ROOT-ONLY ASPIRATION                                                 │
│     Aspiration chỉ áp dụng ở root. Interior nodes cũng có thể          │
│     benefit từ adaptive windows (PVS enhancement).                       │
│                                                                          │
│  ❺ ISOLATED LEARNING                                                    │
│     Learning chỉ within game. Cross-game/cross-position patterns        │
│     không được capture.                                                  │
│                                                                          │
│  ❻ NO CAUSAL UNDERSTANDING                                              │
│     AAW biết score THAY ĐỔI nhưng không biết TẠI SAO.                  │
│     Causal understanding → better prediction.                           │
│                                                                          │
│  ❼ NO ASPIRATION-PRUNING SYNERGY                                       │
│     Aspiration window information không feed back vào pruning           │
│     decisions (LMR, futility, razoring). Missed opportunity.            │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.2 AAW-X Extension Map

```
                        ┌─────────────────────────┐
                        │        AAW-X             │
                        │   Extended Architecture  │
                        └────────┬────────────────┘
                                 │
        ┌────────────────────────┼────────────────────────┐
        │                        │                        │
   ┌────┴────┐            ┌─────┴──────┐           ┌─────┴──────┐
   │ Layer 1 │            │  Layer 2   │           │  Layer 3   │
   │ Score   │            │  Search    │           │  Learning  │
   │ Modeling│            │  Strategy  │           │  & Synergy │
   └────┬────┘            └─────┬──────┘           └─────┬──────┘
        │                       │                        │
   ┌────┴─────────┐       ┌────┴──────────┐       ┌────┴──────────┐
   │ Extension 1  │       │ Extension 3   │       │ Extension 5   │
   │ Bayesian     │       │ Speculative   │       │ Cross-Position│
   │ Score        │       │ Parallel      │       │ Transfer      │
   │ Distribution │       │ Windows       │       │ Learning      │
   ├──────────────┤       ├───────────────┤       ├───────────────┤
   │ Extension 2  │       │ Extension 4   │       │ Extension 6   │
   │ Causal       │       │ Interior Node │       │ Aspiration-   │
   │ Score        │       │ Adaptive      │       │ Pruning       │
   │ Analysis     │       │ Windows       │       │ Synergy       │
   └──────────────┘       ├───────────────┤       ├───────────────┤
                          │ Extension 7   │       │ Extension 8   │
                          │ Adaptive      │       │ Opponent-     │
                          │ Re-Search     │       │ Aware         │
                          │ Depth         │       │ Aspiration    │
                          └───────────────┘       └───────────────┘
```

---

## II. Extension 1: Bayesian Score Distribution Modeling

### 2.1 Vấn Đề Cốt Lõi

```
AAW gốc:
  prev_score = 150cp
  delta = f(context)  → [130, 170]
  
  Giả định ngầm: true score ~ Normal(150, σ²)
  
  Thực tế:
  ┌────────────────────────────────────────────┐
  │ Score distribution thường KHÔNG Normal:     │
  │                                             │
  │ Case 1: Bimodal (sacrifice possible)        │
  │   P(score) có 2 đỉnh: +150 và +350         │
  │   → Window [130,170] miss hoàn toàn +350   │
  │   → Luôn fail high                          │
  │                                             │
  │ Case 2: Heavy left tail (opponent has trap) │
  │   P(score) skewed trái                      │
  │   → Window [130,170] fail low thường xuyên │
  │   → Alpha cần mở rộng hơn nhiều            │
  │                                             │
  │ Case 3: Sharp peak (forced line)            │
  │   Score gần như deterministic = 152 ± 2     │
  │   → Window [130,170] quá rộng → waste      │
  │   → Delta = 5 đủ rồi                       │
  └────────────────────────────────────────────┘
```

### 2.2 Bayesian Score Model

```python
class BayesianScoreModel:
    """Model P(true_score | evidence) as mixture distribution"""
    
    def __init__(self):
        # Mixture of Gaussians to capture multimodal distributions
        self.max_components = 4
        
        # Prior hyperparameters (updated per position)
        self.prior = ScorePrior(
            mean=0.0,
            variance=10000.0,  # Very uncertain prior
            df=3.0,            # Degrees of freedom (heavy tails)
        )
        
        # Accumulated evidence
        self.observations = []
        self.weights = []
    
    def update_with_iteration(self, depth, score, nodes_searched,
                               best_move, position_features):
        """Update posterior after each iteration"""
        
        # ═══ OBSERVATION MODEL ═══
        # 
        # Score at depth d is noisy observation of true score:
        # score_d = true_score + noise_d
        # 
        # noise_d ~ Normal(0, σ²(d))
        # σ²(d) decreases with depth (deeper = more accurate)
        
        observation_variance = self.compute_observation_variance(
            depth, nodes_searched, position_features
        )
        
        observation = ScoreObservation(
            score=score,
            depth=depth,
            variance=observation_variance,
            best_move=best_move,
            weight=self.compute_observation_weight(depth),
            timestamp=len(self.observations),
        )
        
        self.observations.append(observation)
        
        # ═══ POSTERIOR UPDATE ═══
        
        self.posterior = self.compute_posterior()
    
    def compute_observation_variance(self, depth, nodes, features):
        """σ²(d) - Variance of score observation at depth d"""
        
        # Base variance: decreases with depth
        # σ²(d) = σ²_base / (1 + α * d)
        base_variance = 2500.0  # 50cp std at depth 0
        depth_factor = 1.0 + 0.15 * depth
        
        depth_variance = base_variance / depth_factor
        
        # Tactical complexity increases variance
        tactical_factor = 1.0 + features.tactical_volatility * 3.0
        
        # Low node count increases variance (incomplete search)
        expected_nodes = 1000 * (1.7 ** depth)
        node_factor = max(1.0, expected_nodes / max(nodes, 1))
        
        # Move instability increases variance
        move_factor = 1.0 + (1.0 - features.move_stability) * 2.0
        
        total_variance = depth_variance * tactical_factor * \
                         node_factor * move_factor
        
        return total_variance
    
    def compute_observation_weight(self, depth):
        """Weight for observation at depth d"""
        # Recent + deep observations have more weight
        recency_weight = 0.95 ** (len(self.observations))
        depth_weight = min(depth / 15.0, 1.0)
        
        return recency_weight * depth_weight
    
    def compute_posterior(self):
        """Compute posterior distribution P(score | all observations)"""
        
        if len(self.observations) < 2:
            return self.prior
        
        # ═══ STEP 1: DETECT MODALITY ═══
        
        scores = [obs.score for obs in self.observations]
        modality = self.detect_modality(scores)
        
        if modality == 'unimodal':
            return self.compute_unimodal_posterior()
        elif modality == 'bimodal':
            return self.compute_bimodal_posterior()
        else:
            return self.compute_mixture_posterior()
    
    def detect_modality(self, scores):
        """Detect nếu score distribution là multimodal"""
        
        if len(scores) < 4:
            return 'unimodal'
        
        recent = scores[-6:]
        
        # Dip test for unimodality (simplified)
        sorted_scores = sorted(recent)
        
        # Check for gaps > 2σ
        diffs = [sorted_scores[i+1] - sorted_scores[i] 
                 for i in range(len(sorted_scores)-1)]
        
        mean_diff = np.mean(diffs)
        max_diff = max(diffs)
        
        if max_diff > mean_diff * 4.0 and max_diff > 30:
            # Large gap suggests bimodality
            return 'bimodal'
        
        # Check for oscillation pattern (A, B, A, B)
        if len(recent) >= 4:
            alternating = all(
                abs(recent[i] - recent[i+2]) < 15
                for i in range(len(recent)-2)
            ) and all(
                abs(recent[i] - recent[i+1]) > 25
                for i in range(len(recent)-1)
            )
            
            if alternating:
                return 'bimodal'
        
        return 'unimodal'
    
    def compute_unimodal_posterior(self):
        """Conjugate Normal-Normal update"""
        
        # Weighted observations
        weighted_sum = 0.0
        precision_sum = 0.0
        
        for obs in self.observations:
            precision = obs.weight / obs.variance
            weighted_sum += obs.score * precision
            precision_sum += precision
        
        # Prior
        prior_precision = 1.0 / self.prior.variance
        
        # Posterior
        posterior_precision = prior_precision + precision_sum
        posterior_mean = (self.prior.mean * prior_precision + 
                         weighted_sum) / posterior_precision
        posterior_variance = 1.0 / posterior_precision
        
        # Detect skewness
        recent_residuals = [
            obs.score - posterior_mean 
            for obs in self.observations[-5:]
        ]
        skewness = self.compute_skewness(recent_residuals)
        
        return UnimodalPosterior(
            mean=posterior_mean,
            variance=posterior_variance,
            skewness=skewness,
            df=max(self.prior.df, len(self.observations) - 1),
        )
    
    def compute_bimodal_posterior(self):
        """Fit 2-component Gaussian mixture"""
        
        scores = np.array([obs.score for obs in self.observations])
        weights = np.array([obs.weight for obs in self.observations])
        
        # Simple EM for 2 components
        # Initialize with k-means
        sorted_scores = np.sort(scores)
        mid = len(sorted_scores) // 2
        
        mu1 = np.mean(sorted_scores[:mid])
        mu2 = np.mean(sorted_scores[mid:])
        sigma1 = np.std(sorted_scores[:mid]) + 5.0
        sigma2 = np.std(sorted_scores[mid:]) + 5.0
        pi1 = 0.5
        
        # EM iterations (3-5 sufficient)
        for _ in range(5):
            # E-step
            resp1 = pi1 * norm.pdf(scores, mu1, sigma1)
            resp2 = (1-pi1) * norm.pdf(scores, mu2, sigma2)
            total = resp1 + resp2 + 1e-10
            
            gamma1 = (resp1 / total) * weights
            gamma2 = (resp2 / total) * weights
            
            # M-step
            n1 = np.sum(gamma1)
            n2 = np.sum(gamma2)
            
            if n1 > 0.1:
                mu1 = np.sum(gamma1 * scores) / n1
                sigma1 = np.sqrt(
                    np.sum(gamma1 * (scores - mu1)**2) / n1
                ) + 2.0
            
            if n2 > 0.1:
                mu2 = np.sum(gamma2 * scores) / n2
                sigma2 = np.sqrt(
                    np.sum(gamma2 * (scores - mu2)**2) / n2
                ) + 2.0
            
            pi1 = n1 / (n1 + n2)
        
        return BimodalPosterior(
            mu1=mu1, sigma1=sigma1, weight1=pi1,
            mu2=mu2, sigma2=sigma2, weight2=1-pi1,
        )
    
    def compute_skewness(self, residuals):
        """Compute skewness of residuals"""
        if len(residuals) < 3:
            return 0.0
        
        n = len(residuals)
        mean_r = np.mean(residuals)
        std_r = np.std(residuals) + 1e-10
        
        skew = np.mean(((np.array(residuals) - mean_r) / std_r) ** 3)
        return clamp(skew, -2.0, 2.0)


class PosteriorWindowOptimizer:
    """Use posterior to compute OPTIMAL aspiration window"""
    
    def compute_optimal_window(self, posterior, target_confidence=0.80,
                                time_factor=1.0):
        """
        Compute window [α, β] such that:
        P(true_score ∈ [α, β]) = target_confidence
        AND minimizes expected search cost
        """
        
        if isinstance(posterior, UnimodalPosterior):
            return self.optimize_unimodal(
                posterior, target_confidence, time_factor
            )
        elif isinstance(posterior, BimodalPosterior):
            return self.optimize_bimodal(
                posterior, target_confidence, time_factor
            )
    
    def optimize_unimodal(self, posterior, target_conf, time_factor):
        """Optimal window cho unimodal posterior"""
        
        mean = posterior.mean
        std = np.sqrt(posterior.variance)
        skew = posterior.skewness
        
        # ═══ SYMMETRIC CASE (skew ≈ 0) ═══
        
        if abs(skew) < 0.3:
            # Use t-distribution quantile (heavier tails than Normal)
            df = max(posterior.df, 3)
            
            # Adjust confidence by time factor
            # Low time → want higher confidence (avoid re-search)
            adjusted_conf = min(
                target_conf + (time_factor - 1.0) * 0.1,
                0.95
            )
            
            # Quantile
            z = t_distribution.ppf(
                (1 + adjusted_conf) / 2, df=df
            )
            
            delta = z * std
            
            return OptimalWindow(
                alpha=mean - delta,
                beta=mean + delta,
                confidence=adjusted_conf,
                is_symmetric=True,
            )
        
        # ═══ SKEWED CASE ═══
        
        else:
            # Use Cornish-Fisher expansion for skewed quantiles
            #
            # Skew > 0: right-skewed → need more room on right
            # Skew < 0: left-skewed → need more room on left
            
            z_base = norm.ppf((1 + target_conf) / 2)
            
            # Cornish-Fisher correction
            z_lo = z_base + (z_base**2 - 1) * skew / 6
            z_hi = z_base - (z_base**2 - 1) * skew / 6
            
            # Ensure minimum window
            z_lo = max(z_lo, 0.5)
            z_hi = max(z_hi, 0.5)
            
            delta_lo = z_lo * std
            delta_hi = z_hi * std
            
            return OptimalWindow(
                alpha=mean - delta_lo,
                beta=mean + delta_hi,
                confidence=target_conf,
                is_symmetric=False,
                skew_direction='right' if skew > 0 else 'left',
            )
    
    def optimize_bimodal(self, posterior, target_conf, time_factor):
        """Optimal window cho bimodal posterior"""
        
        mu1, sigma1, w1 = posterior.mu1, posterior.sigma1, posterior.weight1
        mu2, sigma2, w2 = posterior.mu2, posterior.sigma2, posterior.weight2
        
        # ═══ STRATEGY SELECTION ═══
        
        gap = abs(mu2 - mu1)
        
        if gap < 2 * (sigma1 + sigma2):
            # Modes overlap → treat as wide unimodal
            
            effective_mean = w1 * mu1 + w2 * mu2
            effective_var = (
                w1 * (sigma1**2 + mu1**2) + 
                w2 * (sigma2**2 + mu2**2) - 
                effective_mean**2
            )
            
            std = np.sqrt(effective_var)
            z = norm.ppf((1 + target_conf) / 2)
            
            return OptimalWindow(
                alpha=effective_mean - z * std,
                beta=effective_mean + z * std,
                confidence=target_conf,
                is_symmetric=True,
                note='merged_bimodal',
            )
        
        else:
            # Modes well-separated
            # 
            # Decision: cover BOTH modes or just DOMINANT mode?
            #
            # Cost analysis:
            #   Cover both: wide window → more nodes but no re-search
            #   Cover dominant: narrow window → fewer nodes but
            #                   P(re-search) = weight of other mode
            
            dominant_mode = 1 if w1 >= w2 else 2
            minor_weight = min(w1, w2)
            
            # Expected cost of covering only dominant mode
            cost_narrow = self.estimate_search_cost_narrow(
                posterior, dominant_mode
            )
            cost_research = minor_weight * cost_narrow * 1.3
            total_narrow = cost_narrow + cost_research
            
            # Expected cost of covering both modes
            cost_wide = self.estimate_search_cost_wide(posterior)
            total_wide = cost_wide
            
            if total_narrow < total_wide * 0.85:
                # Narrow is significantly cheaper
                if dominant_mode == 1:
                    z = norm.ppf((1 + target_conf) / 2)
                    return OptimalWindow(
                        alpha=mu1 - z * sigma1,
                        beta=mu1 + z * sigma1,
                        confidence=target_conf * w1,
                        dominant_mode=1,
                        note='bimodal_narrow',
                    )
                else:
                    z = norm.ppf((1 + target_conf) / 2)
                    return OptimalWindow(
                        alpha=mu2 - z * sigma2,
                        beta=mu2 + z * sigma2,
                        confidence=target_conf * w2,
                        dominant_mode=2,
                        note='bimodal_narrow',
                    )
            
            else:
                # Cover both modes
                lo = min(mu1 - 2*sigma1, mu2 - 2*sigma2)
                hi = max(mu1 + 2*sigma1, mu2 + 2*sigma2)
                
                return OptimalWindow(
                    alpha=lo,
                    beta=hi,
                    confidence=0.95,
                    note='bimodal_wide',
                )
    
    def estimate_search_cost_narrow(self, posterior, mode):
        """Estimate node cost for narrow window"""
        if mode == 1:
            window_size = 4 * posterior.sigma1
        else:
            window_size = 4 * posterior.sigma2
        
        # Narrower window → more cutoffs → fewer nodes
        # Empirical: nodes ∝ window_size^0.3
        return (window_size / 100.0) ** 0.3
    
    def estimate_search_cost_wide(self, posterior):
        """Estimate node cost for wide window"""
        lo = min(posterior.mu1 - 2*posterior.sigma1,
                 posterior.mu2 - 2*posterior.sigma2)
        hi = max(posterior.mu1 + 2*posterior.sigma1,
                 posterior.mu2 + 2*posterior.sigma2)
        
        window_size = hi - lo
        return (window_size / 100.0) ** 0.3
```

### 2.3 Integration Flow

```
┌────────────────────────────────────────────────────────────────┐
│              BAYESIAN ASPIRATION FLOW                          │
│                                                                │
│  Iteration d-2 ──┐                                            │
│  score=148       │                                            │
│                   ▼                                            │
│  Iteration d-1 ──┐  ┌──────────────────────┐                 │
│  score=152       ├──▶│ Bayesian Score Model │                 │
│                   │  │                      │                 │
│  Iteration d ────┘  │ Observations:        │                 │
│  (about to start)    │  d-2: 148 (σ²=400)  │                 │
│                      │  d-1: 152 (σ²=300)  │                 │
│                      │                      │                 │
│                      │ Posterior:           │                 │
│                      │  μ = 151.2          │                 │
│                      │  σ = 12.8           │                 │
│                      │  skew = +0.15       │                 │
│                      └──────────┬───────────┘                 │
│                                 │                              │
│                                 ▼                              │
│                      ┌──────────────────────┐                 │
│                      │ Window Optimizer     │                 │
│                      │                      │                 │
│                      │ target_conf = 0.80   │                 │
│                      │ time_factor = 1.0    │                 │
│                      │                      │                 │
│                      │ Result:              │                 │
│                      │  α = 134.8 (-16.4)  │                 │
│                      │  β = 167.6 (+16.4)  │                 │
│                      │  P(success) = 80%   │                 │
│                      └──────────┬───────────┘                 │
│                                 │                              │
│                                 ▼                              │
│                      ┌──────────────────────┐                 │
│                      │ Search [134.8, 167.6]│                 │
│                      │                      │                 │
│                      │ Result: score = 149  │                 │
│                      │ ✓ Within window      │                 │
│                      └──────────┬───────────┘                 │
│                                 │                              │
│                                 ▼                              │
│                      ┌──────────────────────┐                 │
│                      │ Update posterior     │                 │
│                      │ with observation     │                 │
│                      │ (149, σ²=250)        │                 │
│                      └──────────────────────┘                 │
└────────────────────────────────────────────────────────────────┘
```

---

## III. Extension 2: Causal Score Analysis

### 3.1 Tại Sao Cần Causal Analysis

```
Scenario: Score drops từ +150 → +80 giữa iteration 12 và 13

AAW gốc chỉ thấy: "score giảm 70cp, tăng delta"

Causal analysis hỏi: TẠI SAO giảm 70cp?

Possible causes:
┌──────────────────────────────────────────────────────────────┐
│ Cause A: New tactical refutation found at depth 13          │
│   → Score likely STABLE around 80 ở iterations tiếp theo   │
│   → Small delta cho iteration 14                            │
│                                                              │
│ Cause B: Horizon effect resolved at depth 13                │
│   → Score may continue changing at depth 14                 │
│   → Moderate delta                                           │
│                                                              │
│ Cause C: Best move changed (different line entirely)        │
│   → Score may oscillate back                                │
│   → Large delta, possibly bimodal                           │
│                                                              │
│ Cause D: Evaluation function artifact                       │
│   → Score unreliable, may revert                            │
│   → Very large delta                                         │
└──────────────────────────────────────────────────────────────┘

Each cause → DIFFERENT optimal delta strategy!
```

### 3.2 Causal Score Analyzer

```python
class CausalScoreAnalyzer:
    """Phân tích NGUYÊN NHÂN score changes"""
    
    def __init__(self):
        self.move_change_history = []
        self.pv_change_history = []
        self.eval_component_history = []
    
    def analyze_score_change(self, prev_result, curr_result,
                              prev_depth, curr_depth, position):
        """Phân tích tại sao score thay đổi"""
        
        delta_score = curr_result.score - prev_result.score
        
        if abs(delta_score) < 5:
            return CausalAnalysis(
                cause='stable',
                confidence=0.9,
                expected_future_volatility=5.0,
                recommended_delta_factor=0.7,
            )
        
        # ═══ CAUSE DETECTION ═══
        
        causes = []
        
        # ─── Check 1: Best Move Changed? ───
        
        move_changed = (prev_result.best_move != curr_result.best_move)
        
        if move_changed:
            # How different is the new move?
            move_similarity = self.compute_move_similarity(
                prev_result.best_move, curr_result.best_move, position
            )
            
            if move_similarity < 0.3:
                # Completely different move → likely cause
                causes.append(CausalFactor(
                    type='move_change_major',
                    weight=0.6,
                    volatility_impact=2.5,
                    description=f'Best move changed drastically: '
                                f'{prev_result.best_move} → '
                                f'{curr_result.best_move}',
                ))
            else:
                causes.append(CausalFactor(
                    type='move_change_minor',
                    weight=0.3,
                    volatility_impact=1.3,
                    description='Best move changed to similar move',
                ))
        
        # ─── Check 2: PV Divergence? ───
        
        pv_divergence = self.compute_pv_divergence(
            prev_result.pv, curr_result.pv
        )
        
        if pv_divergence > 0.7:
            causes.append(CausalFactor(
                type='pv_divergence_high',
                weight=0.5,
                volatility_impact=2.0,
                description=f'PV divergence: {pv_divergence:.2f}',
            ))
        elif pv_divergence > 0.3:
            causes.append(CausalFactor(
                type='pv_divergence_moderate',
                weight=0.3,
                volatility_impact=1.5,
            ))
        
        # ─── Check 3: Tactical Resolution? ───
        
        tactical_resolved = self.detect_tactical_resolution(
            prev_result, curr_result, curr_depth
        )
        
        if tactical_resolved:
            causes.append(CausalFactor(
                type='tactical_resolution',
                weight=0.7,
                volatility_impact=0.5,  # Score likely stable now
                description='Tactical sequence resolved at deeper depth',
            ))
        
        # ─── Check 4: Horizon Effect? ───
        
        horizon_effect = self.detect_horizon_effect(
            prev_result, curr_result, position
        )
        
        if horizon_effect:
            causes.append(CausalFactor(
                type='horizon_effect',
                weight=0.5,
                volatility_impact=2.5,  # May continue changing
                description='Possible horizon effect resolution',
            ))
        
        # ─── Check 5: Eval Component Change? ───
        
        eval_change = self.analyze_eval_components(
            prev_result, curr_result, position
        )
        
        if eval_change.significant:
            causes.append(CausalFactor(
                type=f'eval_change_{eval_change.component}',
                weight=0.4,
                volatility_impact=eval_change.expected_continuation,
                description=f'Eval component {eval_change.component} '
                            f'changed by {eval_change.delta}',
            ))
        
        # ─── Check 6: Depth Oscillation Pattern? ───
        
        oscillation = self.detect_oscillation_pattern(
            self.score_history, delta_score
        )
        
        if oscillation.detected:
            causes.append(CausalFactor(
                type='depth_oscillation',
                weight=0.6,
                volatility_impact=1.8,
                description=f'Score oscillating with period '
                            f'{oscillation.period}',
                oscillation_info=oscillation,
            ))
        
        # ═══ SYNTHESIZE CAUSES ═══
        
        return self.synthesize_causal_analysis(
            causes, delta_score, curr_depth
        )
    
    def compute_move_similarity(self, move1, move2, position):
        """How similar are two moves? (0=totally different, 1=same)"""
        
        if move1 == move2:
            return 1.0
        
        score = 0.0
        
        # Same piece type?
        piece1 = position.piece_on(move1.from_sq)
        piece2 = position.piece_on(move2.from_sq)
        if piece1 == piece2:
            score += 0.2
        
        # Same target area? (king-side vs queen-side)
        if file_of(move1.to_sq) // 4 == file_of(move2.to_sq) // 4:
            score += 0.1
        
        # Both captures?
        if position.is_capture(move1) and position.is_capture(move2):
            score += 0.2
        
        # Both checks?
        if position.gives_check(move1) and position.gives_check(move2):
            score += 0.2
        
        # Same from square?
        if move1.from_sq == move2.from_sq:
            score += 0.3
        
        return min(score, 1.0)
    
    def compute_pv_divergence(self, pv1, pv2):
        """How different are two PVs? (0=same, 1=totally different)"""
        
        if not pv1 or not pv2:
            return 1.0
        
        min_len = min(len(pv1), len(pv2))
        matches = sum(1 for i in range(min_len) if pv1[i] == pv2[i])
        
        divergence = 1.0 - matches / max(min_len, 1)
        
        # Weight early moves more (PV divergence at move 1 vs move 10)
        weighted_divergence = 0.0
        total_weight = 0.0
        
        for i in range(min_len):
            weight = 1.0 / (1.0 + i * 0.3)
            if pv1[i] != pv2[i]:
                weighted_divergence += weight
            total_weight += weight
        
        return weighted_divergence / max(total_weight, 0.001)
    
    def detect_tactical_resolution(self, prev_result, curr_result,
                                     curr_depth):
        """Detect if a tactical sequence was resolved"""
        
        # Indicators:
        # 1. Score changed significantly
        # 2. PV contains captures/checks at the end
        # 3. Score stabilized after the change
        
        pv = curr_result.pv
        if not pv or len(pv) < curr_depth:
            return False
        
        # Count tactical moves near the end of PV
        tactical_count = 0
        for move in pv[-4:]:
            if is_capture(move) or is_check(move) or is_promotion(move):
                tactical_count += 1
        
        # If PV ends with many tactical moves at exactly search depth
        # → likely resolution of tactical sequence
        if tactical_count >= 2:
            return True
        
        # If score jumped but best move didn't change
        # → resolved something in deeper tree
        if (abs(curr_result.score - prev_result.score) > 20 and
            curr_result.best_move == prev_result.best_move):
            return True
        
        return False
    
    def detect_horizon_effect(self, prev_result, curr_result, position):
        """Detect if score change is due to horizon effect"""
        
        # Horizon effect indicators:
        # 1. Score change is exactly the value of a piece
        # 2. PV involves delayed capture/exchange
        # 3. Score was "too good" and now corrected
        
        delta = curr_result.score - prev_result.score
        
        # Check if delta matches piece values
        piece_values = [100, 300, 315, 500, 900]
        for pv in piece_values:
            if abs(abs(delta) - pv) < 20:
                return True
        
        return False
    
    def detect_oscillation_pattern(self, score_history, current_delta):
        """Detect if scores are oscillating"""
        
        if len(score_history) < 4:
            return OscillationResult(detected=False)
        
        recent = score_history[-6:]
        deltas = [recent[i] - recent[i-1] for i in range(1, len(recent))]
        
        # Check for sign alternation
        sign_changes = sum(
            1 for i in range(1, len(deltas))
            if deltas[i] * deltas[i-1] < 0
        )
        
        if sign_changes >= len(deltas) * 0.7:
            # Alternating signs → oscillation
            amplitude = np.mean([abs(d) for d in deltas])
            
            return OscillationResult(
                detected=True,
                period=2,
                amplitude=amplitude,
                damping=self.compute_damping(deltas),
            )
        
        return OscillationResult(detected=False)
    
    def compute_damping(self, deltas):
        """Is oscillation damping (converging) or growing?"""
        
        amplitudes = [abs(d) for d in deltas]
        if len(amplitudes) < 3:
            return 1.0
        
        # Compare recent amplitude to earlier
        recent_amp = np.mean(amplitudes[-2:])
        earlier_amp = np.mean(amplitudes[:2])
        
        if earlier_amp < 1.0:
            return 1.0
        
        return recent_amp / earlier_amp  # < 1 = damping, > 1 = growing
    
    def synthesize_causal_analysis(self, causes, delta_score, depth):
        """Combine multiple causes into unified analysis"""
        
        if not causes:
            return CausalAnalysis(
                cause='unknown',
                confidence=0.3,
                expected_future_volatility=abs(delta_score) * 0.7,
                recommended_delta_factor=1.5,
            )
        
        # Sort by weight
        causes.sort(key=lambda c: c.weight, reverse=True)
        primary_cause = causes[0]
        
        # Compute expected future volatility
        total_weight = sum(c.weight for c in causes)
        expected_volatility = sum(
            c.weight * c.volatility_impact * abs(delta_score)
            for c in causes
        ) / total_weight
        
        # Compute recommended delta factor
        avg_volatility_impact = sum(
            c.weight * c.volatility_impact for c in causes
        ) / total_weight
        
        # Special case: oscillation detected
        oscillation_cause = next(
            (c for c in causes if c.type == 'depth_oscillation'), None
        )
        
        if oscillation_cause and oscillation_cause.oscillation_info:
            osc = oscillation_cause.oscillation_info
            
            if osc.damping < 0.8:
                # Oscillation is damping → will converge
                recommended_factor = 0.8 * osc.damping
            else:
                # Oscillation growing or stable → large window
                recommended_factor = 1.5 + osc.amplitude / 50.0
        else:
            recommended_factor = avg_volatility_impact
        
        return CausalAnalysis(
            cause=primary_cause.type,
            all_causes=causes,
            confidence=primary_cause.weight,
            expected_future_volatility=expected_volatility,
            recommended_delta_factor=recommended_factor,
            descriptions=[c.description for c in causes if c.description],
        )
```

### 3.3 Causal → Delta Integration

```python
def compute_delta_with_causal(self, base_delta, causal_analysis):
    """Adjust delta based on causal analysis"""
    
    adjusted_delta = base_delta * causal_analysis.recommended_delta_factor
    
    # ═══ SPECIAL ADJUSTMENTS ═══
    
    cause = causal_analysis.cause
    
    if cause == 'tactical_resolution':
        # Tactics resolved → score should be stable now
        adjusted_delta *= 0.6
    
    elif cause == 'move_change_major':
        # Major move change → very uncertain
        adjusted_delta *= 1.8
    
    elif cause == 'depth_oscillation':
        # Oscillation → need window to cover amplitude
        osc = causal_analysis.all_causes[0].oscillation_info
        min_delta = osc.amplitude * 1.2
        adjusted_delta = max(adjusted_delta, min_delta)
    
    elif cause == 'horizon_effect':
        # Horizon effect → may continue → large window
        adjusted_delta *= 2.0
    
    elif cause == 'stable':
        # Very stable → tight window
        adjusted_delta *= 0.5
    
    return clamp(adjusted_delta, 3, 200)
```

---

## IV. Extension 3: Speculative Parallel Windows

### 4.1 Core Idea

```
Current: Serial aspiration
  Thread 0: search [140, 160] → fail high → search [140, 185] → OK
  Time: T₁ + T₂

Speculative Parallel:
  Thread 0: search [145, 165]     (narrow, optimistic)
  Thread 1: search [130, 180]     (medium, conservative)  
  Thread 2: search [100, 220]     (wide, safety net)
  
  → First valid result wins
  → Other threads abort
  → Expected time: min(T_narrow, T_medium, T_wide)
  
  In practice: T_narrow often wins if score stable
  T_medium wins if modest change
  T_wide only needed if dramatic change
  
  NET: Save 40-60% of re-search time on multi-core systems
```

### 4.2 Implementation

```python
class SpeculativeParallelWindows:
    """Run multiple aspiration windows in parallel"""
    
    def __init__(self, num_threads):
        self.num_threads = num_threads
        self.window_allocator = WindowAllocator()
        self.thread_pool = ThreadPool(num_threads)
    
    def search_parallel(self, position, depth, prev_score,
                         posterior, time_budget):
        """Launch parallel searches with different windows"""
        
        # ═══ STEP 1: ALLOCATE THREADS TO WINDOWS ═══
        
        # How many threads for speculative aspiration?
        # Trade-off: more speculative threads = faster aspiration
        #            but fewer threads for actual search
        
        spec_threads = self.compute_speculative_allocation(
            self.num_threads, posterior, time_budget
        )
        
        search_threads = self.num_threads - spec_threads
        
        # ═══ STEP 2: GENERATE WINDOW PORTFOLIO ═══
        
        windows = self.window_allocator.generate_portfolio(
            posterior, spec_threads, time_budget
        )
        
        # ═══ STEP 3: LAUNCH PARALLEL SEARCHES ═══
        
        cancel_event = threading.Event()
        results = [None] * len(windows)
        
        def search_worker(thread_id, window, result_slot):
            try:
                result = search_with_window(
                    position, depth, 
                    window.alpha, window.beta,
                    threads=search_threads // len(windows),
                    cancel_event=cancel_event,
                )
                
                result_slot[0] = (window, result)
                
                # If this window succeeded, signal others to stop
                if window.alpha < result.score < window.beta:
                    cancel_event.set()
                    
            except SearchCancelled:
                pass
        
        # Launch all
        threads = []
        result_slots = [[None] for _ in windows]
        
        for i, window in enumerate(windows):
            t = threading.Thread(
                target=search_worker,
                args=(i, window, result_slots[i])
            )
            threads.append(t)
            t.start()
        
        # Wait for first success or all complete
        for t in threads:
            t.join()
        
        # ═══ STEP 4: SELECT BEST RESULT ═══
        
        valid_results = [
            (w, r) for slot in result_slots 
            if slot[0] is not None
            for w, r in [slot[0]]
            if w.alpha < r.score < w.beta
        ]
        
        if valid_results:
            # Pick narrowest successful window (most precise)
            valid_results.sort(
                key=lambda wr: wr[0].beta - wr[0].alpha
            )
            return valid_results[0][1]
        
        # All failed → need full window search
        return search_with_window(
            position, depth, -MATE_VALUE, MATE_VALUE,
            threads=self.num_threads,
        )
    
    def compute_speculative_allocation(self, total_threads,
                                        posterior, time_budget):
        """How many threads to allocate for speculation"""
        
        # If very uncertain (wide posterior) → more speculation valuable
        posterior_std = np.sqrt(posterior.variance) \
            if hasattr(posterior, 'variance') else 30
        
        # If time pressure → less speculation (need threads for search)
        if time_budget < 1000:  # < 1 second
            return min(2, total_threads // 4)
        
        if posterior_std > 50:
            # Very uncertain → 3 speculative windows
            return min(3, total_threads // 3)
        elif posterior_std > 20:
            # Moderately uncertain → 2 windows
            return min(2, total_threads // 3)
        else:
            # Pretty certain → 1 extra (just in case)
            return min(1, total_threads // 4)


class WindowAllocator:
    """Generate optimal portfolio of windows for parallel search"""
    
    def generate_portfolio(self, posterior, num_windows, time_budget):
        """Generate num_windows windows that together cover 
           P(score) with minimal total search cost"""
        
        if num_windows == 1:
            return [self.single_window(posterior)]
        
        if isinstance(posterior, UnimodalPosterior):
            return self.portfolio_unimodal(posterior, num_windows)
        elif isinstance(posterior, BimodalPosterior):
            return self.portfolio_bimodal(posterior, num_windows)
    
    def portfolio_unimodal(self, posterior, n):
        """Portfolio for unimodal posterior"""
        
        mean = posterior.mean
        std = np.sqrt(posterior.variance)
        
        if n == 2:
            # Window 1: Tight (80% confidence)
            z1 = 1.28  # 80% CI
            w1 = Window(
                alpha=mean - z1 * std,
                beta=mean + z1 * std,
                priority=1,
                expected_success=0.80,
            )
            
            # Window 2: Wide (98% confidence, safety net)
            z2 = 2.33  # 98% CI
            w2 = Window(
                alpha=mean - z2 * std,
                beta=mean + z2 * std,
                priority=2,
                expected_success=0.98,
            )
            
            return [w1, w2]
        
        elif n == 3:
            # Stratified: tight, medium, wide
            windows = []
            confidences = [0.70, 0.90, 0.99]
            z_values = [1.04, 1.645, 2.576]
            
            for i, (conf, z) in enumerate(zip(confidences, z_values)):
                w = Window(
                    alpha=mean - z * std,
                    beta=mean + z * std,
                    priority=i + 1,
                    expected_success=conf,
                )
                windows.append(w)
            
            return windows
        
        else:
            # Generalize
            confidences = np.linspace(0.65, 0.99, n)
            windows = []
            
            for i, conf in enumerate(confidences):
                z = norm.ppf((1 + conf) / 2)
                w = Window(
                    alpha=mean - z * std,
                    beta=mean + z * std,
                    priority=i + 1,
                    expected_success=conf,
                )
                windows.append(w)
            
            return windows
    
    def portfolio_bimodal(self, posterior, n):
        """Portfolio for bimodal posterior"""
        
        mu1 = posterior.mu1
        mu2 = posterior.mu2
        sigma1 = posterior.sigma1
        sigma2 = posterior.sigma2
        w1 = posterior.weight1
        w2 = posterior.weight2
        
        if n >= 3:
            # One window per mode + one safety
            windows = [
                # Mode 1
                Window(
                    alpha=mu1 - 2 * sigma1,
                    beta=mu1 + 2 * sigma1,
                    priority=1 if w1 >= w2 else 2,
                    expected_success=w1 * 0.95,
                    mode_target=1,
                ),
                # Mode 2
                Window(
                    alpha=mu2 - 2 * sigma2,
                    beta=mu2 + 2 * sigma2,
                    priority=2 if w1 >= w2 else 1,
                    expected_success=w2 * 0.95,
                    mode_target=2,
                ),
                # Safety: covers both
                Window(
                    alpha=min(mu1 - 3*sigma1, mu2 - 3*sigma2),
                    beta=max(mu1 + 3*sigma1, mu2 + 3*sigma2),
                    priority=3,
                    expected_success=0.99,
                    mode_target='both',
                ),
            ]
            return windows[:n]
        
        elif n == 2:
            # Dominant mode + safety
            if w1 >= w2:
                dominant_mu, dominant_sigma = mu1, sigma1
            else:
                dominant_mu, dominant_sigma = mu2, sigma2
            
            return [
                Window(
                    alpha=dominant_mu - 2 * dominant_sigma,
                    beta=dominant_mu + 2 * dominant_sigma,
                    priority=1,
                    expected_success=max(w1, w2) * 0.95,
                ),
                Window(
                    alpha=min(mu1 - 3*sigma1, mu2 - 3*sigma2),
                    beta=max(mu1 + 3*sigma1, mu2 + 3*sigma2),
                    priority=2,
                    expected_success=0.99,
                ),
            ]
```

### 4.3 Thread Coordination Protocol

```python
class SpeculativeCoordinator:
    """Coordinate speculative window searches"""
    
    def __init__(self):
        self.shared_tt = SharedTranspositionTable()
        self.result_queue = PriorityQueue()
        self.abort_signals = {}
    
    def coordinate_search(self, windows, position, depth):
        """
        Key insight: Threads share transposition table!
        
        Thread searching wide window benefits from
        TT entries found by thread searching narrow window.
        
        This creates information flow between threads.
        """
        
        # ═══ LAUNCH ORDER OPTIMIZATION ═══
        
        # Start narrowest window first (highest priority)
        # Its TT entries help wider windows
        
        windows.sort(key=lambda w: w.beta - w.alpha)
        
        # Stagger launches: narrow first, then wider
        launch_delays = [0]
        for i in range(1, len(windows)):
            # Delay wider windows slightly to benefit from TT
            delay = 50 * i  # 50ms stagger per level
            launch_delays.append(delay)
        
        # ═══ SHARED TT PROTOCOL ═══
        
        # All threads share same TT
        # When narrow thread finds exact score → TT entry
        # Wide thread reads this → immediate cutoff
        # 
        # This makes wider threads much faster!
        
        for i, (window, delay) in enumerate(zip(windows, launch_delays)):
            self.schedule_search(
                thread_id=i,
                window=window,
                position=position,
                depth=depth,
                delay_ms=delay,
                shared_tt=self.shared_tt,
            )
        
        # ═══ RESULT COLLECTION ═══
        
        first_valid = self.wait_for_first_valid_result()
        
        if first_valid:
            # Abort remaining threads
            self.abort_all_except(first_valid.thread_id)
            
            # But DON'T discard their TT entries!
            # These entries valuable for next iteration
            
            return first_valid.result
        
        # No valid result → full window
        return self.fallback_full_window(position, depth)
    
    def handle_intermediate_result(self, thread_id, window, 
                                     partial_result):
        """Called when a thread has partial result"""
        
        # If thread A (narrow) fails → useful information for thread B
        
        if partial_result.fail_type == 'fail_low':
            # Score is below window A's alpha
            # Thread B (wider) may already know this → no action needed
            
            # But: we can tell thread C (widest) to prioritize
            # searching moves that refute the fail
            self.hint_thread(
                target_thread='widest',
                hint_type='fail_low_detected',
                fail_score=partial_result.score,
            )
        
        elif partial_result.fail_type == 'fail_high':
            # Score above window A's beta
            # Narrow window too small
            
            # Hint: medium thread should search the good move first
            self.hint_thread(
                target_thread='medium',
                hint_type='fail_high_detected',
                good_move=partial_result.best_move,
                fail_score=partial_result.score,
            )
```

---

## V. Extension 4: Interior Node Adaptive Windows

### 5.1 Beyond Root Aspiration

```
Standard PVS (Principal Variation Search):
  - Root: aspiration window
  - Interior PV nodes: full window
  - Non-PV nodes: zero window (scout search)

Observation: Interior PV nodes ALSO have prev_score from TT/IID
  → Can also use adaptive windows!

Example:
  Root search, depth 15
  ├── Move e4: PV node, depth 14
  │   TT says: score ≈ +150 (from previous iteration)
  │   Normal: search with [α, β] from parent
  │   AAW-X: search with [α_adaptive, β_adaptive] 
  │          where adaptive = based on TT confidence
  │   
  │   If TT entry is depth 12, exact score:
  │   → High confidence → narrow window → faster search
  │   
  │   If TT entry is depth 5, upper bound:
  │   → Low confidence → wider window → avoid re-search
```

### 5.2 Interior Window Adapter

```python
class InteriorWindowAdapter:
    """Adapt windows for interior PV nodes"""
    
    def __init__(self, tt):
        self.tt = tt
        self.interior_stats = InteriorAspirationStats()
    
    def adapt_interior_window(self, position, depth, alpha, beta,
                                node_type):
        """Compute adapted window for interior node"""
        
        # Only for PV nodes at sufficient depth
        if node_type != PV_NODE or depth < 6:
            return alpha, beta  # Don't adapt shallow/non-PV
        
        # ═══ LOOKUP TT ═══
        
        tt_entry = self.tt.probe(position.hash_key)
        
        if tt_entry is None or tt_entry.depth < depth - 4:
            return alpha, beta  # No useful TT info
        
        # ═══ COMPUTE TT CONFIDENCE ═══
        
        tt_confidence = self.compute_tt_confidence(tt_entry, depth)
        
        if tt_confidence < 0.5:
            return alpha, beta  # TT not confident enough
        
        # ═══ NARROW WINDOW BASED ON TT ═══
        
        tt_score = tt_entry.score
        window_size = beta - alpha
        
        # Compute adaptive margin based on TT confidence
        # Higher confidence → narrower margin around TT score
        
        base_margin = window_size * (1.0 - tt_confidence * 0.6)
        
        # Ensure window stays within parent bounds
        adapted_alpha = max(alpha, tt_score - base_margin)
        adapted_beta = min(beta, tt_score + base_margin)
        
        # Safety: never make window narrower than 10cp
        if adapted_beta - adapted_alpha < 10:
            return alpha, beta
        
        # Record for statistics
        self.interior_stats.record_adaptation(
            depth=depth,
            original_window=(alpha, beta),
            adapted_window=(adapted_alpha, adapted_beta),
            tt_confidence=tt_confidence,
        )
        
        return adapted_alpha, adapted_beta
    
    def compute_tt_confidence(self, tt_entry, required_depth):
        """Confidence score for TT entry"""
        
        confidence = 0.0
        
        # Depth ratio
        depth_ratio = tt_entry.depth / max(required_depth, 1)
        confidence += min(depth_ratio, 1.0) * 0.4
        
        # Bound type
        if tt_entry.bound_type == EXACT:
            confidence += 0.3
        elif tt_entry.bound_type == LOWER_BOUND:
            confidence += 0.15
        elif tt_entry.bound_type == UPPER_BOUND:
            confidence += 0.15
        
        # Age (recent entries more reliable)
        age = self.tt.current_generation - tt_entry.generation
        if age <= 1:
            confidence += 0.2
        elif age <= 3:
            confidence += 0.1
        
        # Node count (more nodes = more reliable)
        if tt_entry.nodes_searched > 10000:
            confidence += 0.1
        
        return clamp(confidence, 0.0, 0.95)
    
    def handle_interior_fail(self, position, depth, window, 
                               result, fail_type):
        """Handle fail at interior node"""
        
        # When interior aspiration fails:
        # Option A: Fall back to parent window (safe)
        # Option B: Expand adaptively (risky but faster)
        
        # Conservative approach: fall back to parent
        # But record the failure for learning
        
        self.interior_stats.record_failure(
            depth=depth,
            window=window,
            fail_type=fail_type,
            score=result.score,
        )
        
        # After enough failures at this position → 
        # don't adapt next time
        failure_rate = self.interior_stats.get_failure_rate(
            position.hash_key
        )
        
        if failure_rate > 0.4:
            # Too many failures → mark position as unadaptable
            self.interior_stats.mark_unadaptable(position.hash_key)
        
        # Return: redo search with original parent window
        return REDO_WITH_PARENT_WINDOW


class InteriorAspirationStats:
    """Track interior aspiration performance"""
    
    def __init__(self):
        self.adaptations = 0
        self.successes = 0
        self.failures = 0
        self.nodes_saved = 0
        self.nodes_wasted = 0
        self.per_depth_stats = defaultdict(
            lambda: {'adapted': 0, 'success': 0, 'fail': 0}
        )
    
    def is_profitable(self, depth):
        """Is interior aspiration profitable at this depth?"""
        
        stats = self.per_depth_stats[depth]
        
        if stats['adapted'] < 10:
            return True  # Not enough data, default to yes
        
        success_rate = stats['success'] / stats['adapted']
        
        # Need success rate > 70% to be profitable
        # (each failure costs ~30% extra nodes)
        return success_rate > 0.70
    
    def get_optimal_depth_threshold(self):
        """Below which depth should we NOT adapt?"""
        
        for depth in range(4, 20):
            if not self.is_profitable(depth):
                return depth + 1
        
        return 4  # Adapt at all depths
```

---

## VI. Extension 5: Cross-Position Transfer Learning

### 6.1 Position Similarity Clustering

```python
class PositionSimilarityEngine:
    """Cluster similar positions to transfer aspiration knowledge"""
    
    def __init__(self):
        # Feature extractor for position similarity
        self.feature_dim = 24
        
        # Cluster database
        self.clusters = PositionClusterDB(max_clusters=1000)
        
        # Aspiration knowledge per cluster
        self.cluster_knowledge = defaultdict(ClusterAspirationKnowledge)
    
    def extract_aspiration_features(self, position):
        """Extract features relevant for aspiration behavior"""
        
        features = np.zeros(self.feature_dim)
        
        # ─── Material features (6) ───
        features[0] = count_pieces(position, PAWN) / 16.0
        features[1] = count_pieces(position, KNIGHT) / 4.0
        features[2] = count_pieces(position, BISHOP) / 4.0
        features[3] = count_pieces(position, ROOK) / 4.0
        features[4] = count_pieces(position, QUEEN) / 2.0
        features[5] = material_balance(position) / 3000.0
        
        # ─── Structure features (6) ───
        features[6] = pawn_structure_hash(position) % 256 / 256.0
        features[7] = king_safety_score(position, WHITE) / 500.0
        features[8] = king_safety_score(position, BLACK) / 500.0
        features[9] = mobility_score(position) / 100.0
        features[10] = pawn_tension(position) / 10.0
        features[11] = piece_activity(position) / 200.0
        
        # ─── Tactical features (6) ───
        features[12] = hanging_pieces(position) / 6.0
        features[13] = available_checks(position) / 10.0
        features[14] = fork_opportunities(position) / 5.0
        features[15] = pin_count(position) / 4.0
        features[16] = passed_pawn_count(position) / 4.0
        features[17] = promotion_threats(position) / 4.0
        
        # ─── Phase features (3) ───
        features[18] = game_phase(position)  # 0=opening, 1=endgame
        features[19] = pieces_on_board(position) / 32.0
        features[20] = queens_on_board(position) / 2.0
        
        # ─── Historical features (3) ───
        features[21] = position.halfmove_clock / 100.0
        features[22] = position.fullmove_number / 80.0
        features[23] = position.castling_rights / 15.0
        
        return features
    
    def find_similar_cluster(self, features, threshold=0.85):
        """Find most similar cluster"""
        
        best_cluster = None
        best_similarity = 0.0
        
        for cluster in self.clusters.all():
            similarity = self.compute_similarity(
                features, cluster.centroid
            )
            
            if similarity > best_similarity:
                best_similarity = similarity
                best_cluster = cluster
        
        if best_similarity >= threshold:
            return best_cluster
        
        return None
    
    def compute_similarity(self, features1, features2):
        """Cosine similarity with feature weighting"""
        
        # Weight tactical and structure features more heavily
        weights = np.array([
            0.5, 0.5, 0.5, 0.5, 0.5, 0.8,  # Material
            0.7, 1.0, 1.0, 0.8, 0.9, 0.7,    # Structure
            1.2, 1.0, 1.0, 0.8, 0.9, 1.0,     # Tactical
            0.6, 0.5, 0.8,                      # Phase
            0.2, 0.2, 0.3,                      # Historical
        ])
        
        weighted1 = features1 * weights
        weighted2 = features2 * weights
        
        dot = np.dot(weighted1, weighted2)
        norm1 = np.linalg.norm(weighted1)
        norm2 = np.linalg.norm(weighted2)
        
        if norm1 < 1e-10 or norm2 < 1e-10:
            return 0.0
        
        return dot / (norm1 * norm2)
    
    def transfer_aspiration_knowledge(self, position, search_state):
        """Transfer aspiration knowledge from similar positions"""
        
        features = self.extract_aspiration_features(position)
        cluster = self.find_similar_cluster(features)
        
        if cluster is None:
            return None
        
        knowledge = self.cluster_knowledge[cluster.id]
        
        if knowledge.sample_count < 5:
            return None
        
        return TransferredKnowledge(
            recommended_delta=knowledge.avg_optimal_delta,
            expected_fail_rate=knowledge.avg_fail_rate,
            expected_volatility=knowledge.avg_score_volatility,
            expansion_factor=knowledge.avg_expansion_factor,
            confidence=min(knowledge.sample_count / 30.0, 0.9),
            cluster_id=cluster.id,
        )
    
    def update_cluster_knowledge(self, position, aspiration_result):
        """Update cluster knowledge after aspiration search"""
        
        features = self.extract_aspiration_features(position)
        cluster = self.find_similar_cluster(features)
        
        if cluster is None:
            # Create new cluster
            cluster = self.clusters.create_cluster(features)
        
        # Update cluster centroid (online mean)
        cluster.update_centroid(features)
        
        # Update aspiration knowledge
        knowledge = self.cluster_knowledge[cluster.id]
        knowledge.update(aspiration_result)


class ClusterAspirationKnowledge:
    """Aspiration knowledge accumulated for a cluster"""
    
    def __init__(self):
        self.sample_count = 0
        self.avg_optimal_delta = 15.0
        self.avg_fail_rate = 0.25
        self.avg_score_volatility = 20.0
        self.avg_expansion_factor = 1.5
        self.avg_research_ratio = 0.3
    
    def update(self, aspiration_result):
        """Update with new observation"""
        
        self.sample_count += 1
        lr = 1.0 / min(self.sample_count, 50)  # Decay learning rate
        
        # Update running averages
        self.avg_optimal_delta = (
            (1 - lr) * self.avg_optimal_delta + 
            lr * aspiration_result.optimal_delta
        )
        
        self.avg_fail_rate = (
            (1 - lr) * self.avg_fail_rate +
            lr * float(aspiration_result.had_fail)
        )
        
        self.avg_score_volatility = (
            (1 - lr) * self.avg_score_volatility +
            lr * aspiration_result.score_change
        )
        
        self.avg_expansion_factor = (
            (1 - lr) * self.avg_expansion_factor +
            lr * aspiration_result.expansion_used
        )
```

### 6.2 Cross-Game Persistence

```python
class AspirationKnowledgeStore:
    """Persist aspiration knowledge across games"""
    
    def __init__(self, db_path='aspiration_knowledge.db'):
        self.db_path = db_path
        self.similarity_engine = PositionSimilarityEngine()
    
    def save_game_knowledge(self, game_aspiration_data):
        """Save aspiration knowledge from completed game"""
        
        for position_data in game_aspiration_data:
            features = position_data['features']
            results = position_data['aspiration_results']
            
            # Find or create cluster
            cluster = self.similarity_engine.find_similar_cluster(
                features, threshold=0.80
            )
            
            if cluster is None:
                cluster = self.similarity_engine.clusters.create_cluster(
                    features
                )
            
            # Update knowledge
            for result in results:
                self.similarity_engine.update_cluster_knowledge(
                    cluster, result
                )
        
        # Persist to disk
        self.persist_to_disk()
    
    def load_knowledge(self):
        """Load knowledge at engine startup"""
        
        if os.path.exists(self.db_path):
            data = load_from_disk(self.db_path)
            
            self.similarity_engine.clusters = data['clusters']
            self.similarity_engine.cluster_knowledge = data['knowledge']
            
            logging.info(
                f"Loaded aspiration knowledge: "
                f"{len(data['clusters'])} clusters, "
                f"{sum(k.sample_count for k in data['knowledge'].values())} "
                f"samples"
            )
    
    def get_startup_recommendation(self, position):
        """Get aspiration recommendation at game start"""
        
        return self.similarity_engine.transfer_aspiration_knowledge(
            position, search_state=None
        )
```

---

## VII. Extension 6: Aspiration-Pruning Synergy

### 7.1 Bidirectional Information Flow

```
Current: Aspiration and Pruning are INDEPENDENT

   Aspiration → Window [α, β]
   Pruning → LMR, Futility, Razoring (use α, β directly)
   
   NO feedback from aspiration experience TO pruning parameters

AAW-X Synergy:

   ┌──────────────┐         ┌──────────────┐
   │  Aspiration   │ ──────▶ │   Pruning    │
   │  Information  │         │  Parameters  │
   │               │ ◀────── │              │
   │  - Window     │         │  - LMR depth │
   │  - Confidence │         │  - Futility  │
   │  - Fail type  │         │    margin    │
   │  - Trend      │         │  - Razoring  │
   │  - Volatility │         │    threshold │
   └──────────────┘         └──────────────┘
```

### 7.2 Implementation

```python
class AspirationPruningSynergy:
    """Bidirectional synergy between aspiration and pruning"""
    
    def __init__(self):
        self.aspiration_state = None
        self.pruning_adjustments = PruningAdjustments()
    
    def update_from_aspiration(self, aspiration_state):
        """Update pruning parameters based on aspiration state"""
        
        self.aspiration_state = aspiration_state
        
        # ═══ ADJUSTMENT 1: LMR DEPTH REDUCTION ═══
        
        # If aspiration window is very tight (high confidence):
        #   → We expect score close to prev_score
        #   → Can be more aggressive with LMR
        #   → Reduce more for late moves
        
        # If aspiration window is wide (low confidence):
        #   → Score uncertain → be less aggressive with LMR
        #   → Risk missing important moves if we reduce too much
        
        window_confidence = aspiration_state.window_confidence
        
        if window_confidence > 0.85:
            # High confidence → more aggressive LMR
            self.pruning_adjustments.lmr_extra_reduction = 0.5
        elif window_confidence < 0.5:
            # Low confidence → less LMR
            self.pruning_adjustments.lmr_extra_reduction = -0.3
        else:
            self.pruning_adjustments.lmr_extra_reduction = 0.0
        
        # ═══ ADJUSTMENT 2: FUTILITY MARGIN ═══
        
        # If we're in a re-search (aspiration failed):
        #   → We already know approximate score range
        #   → Can use this for tighter futility
        
        if aspiration_state.is_research:
            # Tighter futility during re-search
            self.pruning_adjustments.futility_margin_factor = 0.85
        else:
            self.pruning_adjustments.futility_margin_factor = 1.0
        
        # If score trending strongly:
        #   → Futility in direction of trend can be more aggressive
        
        if aspiration_state.trend > 10:
            # Score improving → moves that look bad are likely bad
            self.pruning_adjustments.futility_margin_factor *= 0.90
        elif aspiration_state.trend < -10:
            # Score worsening → be more careful
            self.pruning_adjustments.futility_margin_factor *= 1.10
        
        # ═══ ADJUSTMENT 3: RAZORING THRESHOLD ═══
        
        # If aspiration says score is very stable:
        #   → Can razor more aggressively
        #   → Unlikely to find big surprises
        
        if aspiration_state.score_volatility < 10:
            self.pruning_adjustments.razoring_threshold_factor = 0.85
        elif aspiration_state.score_volatility > 50:
            self.pruning_adjustments.razoring_threshold_factor = 1.20
        else:
            self.pruning_adjustments.razoring_threshold_factor = 1.0
        
        # ═══ ADJUSTMENT 4: NULL MOVE PRUNING ═══
        
        # If aspiration trend is positive (score improving):
        #   → Null move pruning is more likely to succeed
        #   → Can use smaller reduction
        
        if aspiration_state.trend > 15:
            self.pruning_adjustments.null_move_reduction_bonus = 1
        else:
            self.pruning_adjustments.null_move_reduction_bonus = 0
        
        # ═══ ADJUSTMENT 5: SINGULAR EXTENSION ═══
        
        # If aspiration window is tight and best move is stable:
        #   → Singular extension more valuable (protect the only good move)
        #   → Lower singular margin
        
        if (aspiration_state.window_confidence > 0.8 and
            aspiration_state.move_stability > 0.9):
            self.pruning_adjustments.singular_margin_factor = 0.80
        else:
            self.pruning_adjustments.singular_margin_factor = 1.0
    
    def update_from_pruning(self, pruning_stats):
        """Update aspiration based on pruning behavior"""
        
        # ═══ REVERSE FLOW: Pruning → Aspiration ═══
        
        # If many moves are being futility-pruned:
        #   → Position likely quiet → reduce aspiration delta
        
        futility_rate = pruning_stats.futility_prune_rate
        
        if futility_rate > 0.7:
            # Most moves futility-pruned → very quiet
            self.aspiration_delta_adjustment = 0.7
        elif futility_rate < 0.2:
            # Few moves pruned → complex/tactical
            self.aspiration_delta_adjustment = 1.4
        else:
            self.aspiration_delta_adjustment = 1.0
        
        # If null move pruning succeeds frequently:
        #   → Score is likely above beta → trend positive
        #   → Aspiration can be asymmetric (wider above)
        
        if pruning_stats.null_move_success_rate > 0.8:
            self.aspiration_asymmetry_hint = 'widen_above'
        elif pruning_stats.null_move_success_rate < 0.3:
            self.aspiration_asymmetry_hint = 'widen_below'
        else:
            self.aspiration_asymmetry_hint = 'symmetric'
    
    def get_lmr_adjustment(self, depth, move_index, node_type):
        """Get LMR adjustment for current aspiration state"""
        
        base_adjustment = self.pruning_adjustments.lmr_extra_reduction
        
        # Scale with depth (bigger effect at deeper nodes)
        depth_scale = min(depth / 10.0, 1.5)
        
        # Scale with move index (bigger effect for later moves)
        move_scale = min(move_index / 10.0, 1.5)
        
        return base_adjustment * depth_scale * move_scale
    
    def get_futility_margin(self, depth, base_margin):
        """Get adjusted futility margin"""
        
        return int(
            base_margin * 
            self.pruning_adjustments.futility_margin_factor
        )
    
    def get_razoring_threshold(self, depth, base_threshold):
        """Get adjusted razoring threshold"""
        
        return int(
            base_threshold *
            self.pruning_adjustments.razoring_threshold_factor
        )


class PruningAdjustments:
    """Container for pruning parameter adjustments"""
    
    def __init__(self):
        self.lmr_extra_reduction = 0.0
        self.futility_margin_factor = 1.0
        self.razoring_threshold_factor = 1.0
        self.null_move_reduction_bonus = 0
        self.singular_margin_factor = 1.0
```

---

## VIII. Extension 7: Adaptive Re-Search Depth

### 8.1 Core Concept

```
Current: Re-search always uses SAME depth as original search

Problem:
  Original search: depth 15, 50M nodes
  Fail high → re-search: depth 15, 45M nodes (wasted!)
  
  BUT: Do we NEED depth 15 for re-search?
  
  If fail was small (score = beta + 5):
    → Score is close to window → depth 14 re-search may suffice
    → Saves 30-50% nodes
    
  If fail was large (score = beta + 100):
    → Score is far from window → even depth 12-13 would give 
       enough info to set next window correctly
    → Saves 60-80% nodes
    
  Key insight: Re-search DOESN'T need to be as deep as original
  search — it just needs to narrow down score range for the
  NEXT aspiration attempt.
```

### 8.2 Implementation

```python
class AdaptiveReSearchDepth:
    """Adaptively choose re-search depth"""
    
    def __init__(self):
        self.depth_performance = defaultdict(
            lambda: {'attempts': 0, 'successes': 0, 'savings': 0}
        )
    
    def compute_research_depth(self, original_depth, fail_type, 
                                 fail_magnitude, window, 
                                 score_analysis, time_factor):
        """Compute optimal depth for re-search"""
        
        # ═══ FACTOR 1: FAIL MAGNITUDE ═══
        
        # Small fail → need precise re-search → keep depth
        # Large fail → just need approximate → reduce depth
        
        if fail_magnitude < 10:
            magnitude_reduction = 0
        elif fail_magnitude < 30:
            magnitude_reduction = 1
        elif fail_magnitude < 80:
            magnitude_reduction = 2
        else:
            magnitude_reduction = 3
        
        # ═══ FACTOR 2: EXPANSION COUNT ═══
        
        # First re-search → keep depth (try to get it right)
        # Later re-searches → reduce depth (just find score range)
        
        expansion_reduction = min(window.expansion_count, 2)
        
        # ═══ FACTOR 3: SCORE CONFIDENCE ═══
        
        # If we're very uncertain → deeper re-search needed
        if score_analysis.confidence < 0.4:
            confidence_adjustment = -1  # Need MORE depth
        elif score_analysis.confidence > 0.8:
            confidence_adjustment = 1   # Can afford LESS depth
        else:
            confidence_adjustment = 0
        
        # ═══ FACTOR 4: TIME PRESSURE ═══
        
        if time_factor > 1.5:
            time_reduction = 2  # Aggressive depth reduction
        elif time_factor > 1.2:
            time_reduction = 1
        else:
            time_reduction = 0
        
        # ═══ COMPUTE FINAL DEPTH ═══
        
        total_reduction = (
            magnitude_reduction +
            expansion_reduction +
            confidence_adjustment +
            time_reduction
        )
        
        research_depth = original_depth - total_reduction
        
        # Safety: never reduce below original_depth - 4
        research_depth = max(research_depth, original_depth - 4)
        
        # Safety: never reduce below depth 6
        research_depth = max(research_depth, 6)
        
        return research_depth
    
    def should_verify_at_full_depth(self, research_result, 
                                      original_depth, research_depth):
        """After reduced-depth re-search, should we verify?"""
        
        # If research depth was same as original → no need
        if research_depth >= original_depth:
            return False
        
        depth_gap = original_depth - research_depth
        
        # If small gap (1-2 plies) → usually OK, no verification
        if depth_gap <= 2:
            return False
        
        # If large gap AND result is close to window edge
        # → verify at full depth to be sure
        # (This is rare and only for important decisions)
        
        if depth_gap >= 3 and research_result.score_near_boundary:
            return True
        
        return False
    
    def record_depth_performance(self, original_depth, research_depth,
                                    was_successful, nodes_saved):
        """Record performance of depth reduction"""
        
        reduction = original_depth - research_depth
        stats = self.depth_performance[reduction]
        
        stats['attempts'] += 1
        if was_successful:
            stats['successes'] += 1
        stats['savings'] += nodes_saved
    
    def get_optimal_reduction(self, fail_magnitude):
        """Get historically optimal reduction for given fail magnitude"""
        
        best_reduction = 0
        best_efficiency = 0
        
        for reduction, stats in self.depth_performance.items():
            if stats['attempts'] < 5:
                continue
            
            success_rate = stats['successes'] / stats['attempts']
            avg_savings = stats['savings'] / stats['attempts']
            
            # Efficiency = savings * success_rate
            efficiency = avg_savings * success_rate
            
            if efficiency > best_efficiency:
                best_efficiency = efficiency
                best_reduction = reduction
        
        return best_reduction
```

---

## IX. Extension 8: Opponent-Aware Aspiration

### 9.1 Core Idea

```
Standard aspiration: "What is the objective best score?"

Opponent-aware: "What score should we expect given the opponent?"

Against weaker opponent:
  → More likely to achieve advantages
  → Score tends to be higher than minimax
  → Could widen upper bound (expect windfall gains)

Against stronger opponent:
  → Fewer tactical opportunities
  → Score tends to converge to theoretical value
  → Tighter windows might be appropriate

Against aggressive opponent:
  → More tactical positions → higher volatility
  → Need wider windows

Against positional opponent:
  → Quieter positions → lower volatility
  → Can use tighter windows
```

### 9.2 Implementation

```python
class OpponentAwareAspiration:
    """Adjust aspiration based on opponent model"""
    
    def __init__(self):
        self.opponent_model = OpponentModel()
        self.game_score_history = []
    
    def adjust_delta_for_opponent(self, base_delta, position,
                                    search_state):
        """Adjust delta based on opponent characteristics"""
        
        if not self.opponent_model.is_calibrated:
            return base_delta
        
        # ═══ STYLE-BASED ADJUSTMENT ═══
        
        style = self.opponent_model.estimated_style
        
        if style == 'tactical':
            # Tactical opponent → more volatile positions
            style_factor = 1.3
        elif style == 'positional':
            # Positional opponent → more stable positions
            style_factor = 0.85
        elif style == 'solid':
            # Solid opponent → very stable
            style_factor = 0.75
        else:
            style_factor = 1.0
        
        # ═══ STRENGTH-BASED ADJUSTMENT ═══
        
        # Against much weaker → expect windfall gains
        # → asymmetric window: widen upward
        
        strength_diff = self.opponent_model.estimated_elo_diff
        
        if strength_diff > 300:
            # Much weaker → expect positive surprises
            asymmetry = 1.3  # Widen upper bound
        elif strength_diff < -300:
            # Much stronger → expect score close to theoretical
            asymmetry = 1.0  # Symmetric (minimax is accurate)
        else:
            asymmetry = 1.0
        
        # ═══ GAME PHASE ADJUSTMENT ═══
        
        moves_played = len(self.game_score_history)
        
        if moves_played < 10:
            # Opening: opponent prep might surprise us
            phase_factor = 1.2
        elif moves_played > 40:
            # Endgame: more deterministic
            phase_factor = 0.9
        else:
            phase_factor = 1.0
        
        # ═══ RECENT TREND IN GAME ═══
        
        if len(self.game_score_history) >= 5:
            recent_scores = self.game_score_history[-5:]
            game_trend = recent_scores[-1] - recent_scores[0]
            
            if game_trend > 100:
                # Winning strongly → opponent may try desperate tactics
                # → more volatile → wider window
                trend_factor = 1.3
            elif game_trend < -100:
                # Losing → we may try desperate tactics
                trend_factor = 1.3
            else:
                trend_factor = 1.0
        else:
            trend_factor = 1.0
        
        # ═══ COMBINE ═══
        
        adjusted_delta = base_delta * style_factor * phase_factor * \
                         trend_factor
        
        return adjusted_delta, asymmetry
    
    def update_opponent_model(self, position, our_eval, time_used,
                               opponent_move):
        """Update opponent model after their move"""
        
        # Track opponent's time usage
        self.opponent_model.update_time_pattern(time_used)
        
        # Track if opponent plays "best" moves
        if our_eval is not None:
            # Compare opponent's move to our expected move
            expected_score_change = self.estimate_score_change(
                position, opponent_move
            )
            
            self.opponent_model.update_play_quality(
                expected_score_change
            )
        
        # Track position complexity chosen by opponent
        complexity = compute_position_complexity(position)
        self.opponent_model.update_style_estimate(complexity)


class OpponentModel:
    """Simple opponent model for aspiration adjustment"""
    
    def __init__(self):
        self.estimated_style = 'unknown'
        self.estimated_elo_diff = 0
        self.is_calibrated = False
        
        self.complexity_history = []
        self.quality_history = []
        self.time_history = []
    
    def update_style_estimate(self, complexity):
        """Update style estimate based on position complexity"""
        
        self.complexity_history.append(complexity)
        
        if len(self.complexity_history) >= 10:
            avg_complexity = np.mean(self.complexity_history[-10:])
            
            if avg_complexity > 0.7:
                self.estimated_style = 'tactical'
            elif avg_complexity < 0.3:
                self.estimated_style = 'positional'
            elif avg_complexity < 0.5:
                self.estimated_style = 'solid'
            else:
                self.estimated_style = 'balanced'
            
            self.is_calibrated = True
    
    def update_play_quality(self, score_impact):
        """Update estimate of opponent strength"""
        
        self.quality_history.append(score_impact)
        
        if len(self.quality_history) >= 10:
            avg_quality = np.mean(self.quality_history[-10:])
            
            # Map quality to elo difference estimate
            # Negative quality = opponent plays worse than engine
            self.estimated_elo_diff = -avg_quality * 3
```

---

## X. Unified AAW-X Architecture

### 10.1 Complete Integration

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        AAW-X COMPLETE ARCHITECTURE                       │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │                    EVIDENCE LAYER (Extended)                        ││
│  │                                                                     ││
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ ┌──────────┐ ││
│  │  │Position  │ │ Bayesian │ │ Causal   │ │ Time   │ │ Opponent │ ││
│  │  │Context   │ │ Score    │ │ Score    │ │Pressure│ │ Model    │ ││
│  │  │Analyzer  │ │ Model    │ │ Analyzer │ │Monitor │ │          │ ││
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └───┬────┘ └────┬─────┘ ││
│  │       └─────┬───────┴───────┬────┴───────┬───┘───────────┘       ││
│  │             ▼               ▼            ▼                        ││
│  │      ┌──────────────────────────────────────────┐                ││
│  │      │   Posterior Distribution P(score|E)      │                ││
│  │      │   + Causal Analysis                       │                ││
│  │      │   + Transfer Knowledge                    │                ││
│  │      └────────────────┬──────────────────────────┘                ││
│  └───────────────────────┼──────────────────────────────────────────┘│
│                          ▼                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │                 WINDOW COMPUTATION (Extended)                      ││
│  │                                                                     ││
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────┐  ││
│  │  │ Optimal Window   │  │ Window Portfolio  │  │ Interior Node │  ││
│  │  │ from Posterior   │  │ for Parallel      │  │ Adaptation    │  ││
│  │  │ (skew-aware)     │  │ Speculation       │  │               │  ││
│  │  └────────┬─────────┘  └────────┬──────────┘  └───────┬───────┘  ││
│  │           └────────┬────────────┘─────────────────────┘          ││
│  │                    ▼                                              ││
│  │      ┌──────────────────────────────────────────┐                ││
│  │      │   Window(s) [α₁,β₁], [α₂,β₂], ...     │                ││
│  │      └────────────────┬──────────────────────────┘                ││
│  └───────────────────────┼──────────────────────────────────────────┘│
│                          ▼                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │              SEARCH EXECUTION (Extended)                           ││
│  │                                                                     ││
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────┐  ││
│  │  │ Speculative      │  │ Adaptive         │  │ Aspiration-    │  ││
│  │  │ Parallel Search  │  │ Re-Search Depth  │  │ Pruning        │  ││
│  │  │ (multi-window)   │  │                  │  │ Synergy        │  ││
│  │  └────────┬─────────┘  └────────┬──────────┘  └───────┬───────┘  ││
│  │           └────────┬────────────┘─────────────────────┘          ││
│  │                    ▼                                              ││
│  │      ┌──────────────────────────────────────────┐                ││
│  │      │   Search Result + Tree Info              │                ││
│  │      └────────────────┬──────────────────────────┘                ││
│  └───────────────────────┼──────────────────────────────────────────┘│
│                          ▼                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │              LEARNING & ADAPTATION (Extended)                      ││
│  │                                                                     ││
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────┐  ││
│  │  │ Cross-Position   │  │ Posterior         │  │ Online         │  ││
│  │  │ Transfer         │  │ Update &          │  │ Parameter      │  ││
│  │  │ Learning         │  │ Calibration       │  │ Tuning         │  ││
│  │  └──────────────────┘  └──────────────────┘  └────────────────┘  ││
│  └─────────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────────┘
```

### 10.2 Main Loop — AAW-X Complete

```python
def aspiration_search_aaw_x(position, search_state, engine_config):
    """Complete AAW-X aspiration search"""
    
    # ═══════════════════════════════════════════
    # PHASE 0: EVIDENCE COLLECTION
    # ═══════════════════════════════════════════
    
    # [Original AAW] Position context
    position_profile = position_analyzer.analyze(position, search_state)
    
    # [Extension 1] Bayesian score model
    bayesian_model.update_with_iteration(
        depth=search_state.iteration - 1,
        score=search_state.prev_score,
        nodes_searched=search_state.prev_nodes,
        best_move=search_state.prev_best_move,
        position_features=position_profile,
    )
    posterior = bayesian_model.compute_posterior()
    
    # [Extension 2] Causal analysis
    causal = None
    if search_state.iteration >= 3:
        causal = causal_analyzer.analyze_score_change(
            prev_result=search_state.prev_result,
            curr_result=search_state.second_prev_result,
            prev_depth=search_state.iteration - 2,
            curr_depth=search_state.iteration - 1,
            position=position,
        )
    
    # [Original AAW] Time pressure
    time_factor = time_monitor.get_time_pressure_factor(search_state)
    
    # [Extension 5] Cross-position transfer
    transferred = similarity_engine.transfer_aspiration_knowledge(
        position, search_state
    )
    
    # [Extension 8] Opponent model
    opponent_adjustment, asymmetry = opponent_model.adjust_delta_for_opponent(
        base_delta=0,  # Will be computed
        position=position,
        search_state=search_state,
    )
    
    # ═══════════════════════════════════════════
    # PHASE 1: WINDOW COMPUTATION
    # ═══════════════════════════════════════════
    
    # [Extension 1] Optimal window from posterior
    target_confidence = compute_target_confidence(
        time_factor, search_state.iteration
    )
    
    optimal_window = window_optimizer.compute_optimal_window(
        posterior, target_confidence, time_factor
    )
    
    # [Extension 2] Adjust with causal analysis
    if causal:
        causal_factor = causal.recommended_delta_factor
        optimal_window.alpha = (
            posterior.mean - 
            (posterior.mean - optimal_window.alpha) * causal_factor
        )
        optimal_window.beta = (
            posterior.mean + 
            (optimal_window.beta - posterior.mean) * causal_factor
        )
    
    # [Extension 5] Blend with transferred knowledge
    if transferred and transferred.confidence > 0.5:
        blend_factor = transferred.confidence * 0.3
        
        transfer_delta = transferred.recommended_delta
        current_delta_lo = posterior.mean - optimal_window.alpha
        current_delta_hi = optimal_window.beta - posterior.mean
        
        optimal_window.alpha = posterior.mean - (
            current_delta_lo * (1 - blend_factor) + 
            transfer_delta * blend_factor
        )
        optimal_window.beta = posterior.mean + (
            current_delta_hi * (1 - blend_factor) + 
            transfer_delta * blend_factor
        )
    
    # [Extension 8] Opponent asymmetry
    if asymmetry != 1.0:
        # Widen in direction of expected advantage
        if asymmetry > 1.0:
            optimal_window.beta += (
                (optimal_window.beta - posterior.mean) * 
                (asymmetry - 1.0)
            )
        else:
            optimal_window.alpha -= (
                (posterior.mean - optimal_window.alpha) * 
                (1.0 - asymmetry)
            )
    
    # ═══════════════════════════════════════════
    # PHASE 2: SEARCH EXECUTION
    # ═══════════════════════════════════════════
    
    # [Extension 3] Decide: serial or parallel aspiration?
    
    use_parallel = (
        engine_config.num_threads >= 4 and
        posterior.variance > 400 and  # Uncertain enough to benefit
        time_factor < 1.5  # Not too time-pressured
    )
    
    if use_parallel:
        # Speculative parallel search
        result = speculative_parallel.search_parallel(
            position=position,
            depth=search_state.target_depth,
            prev_score=search_state.prev_score,
            posterior=posterior,
            time_budget=time_monitor.remaining_time,
        )
    
    else:
        # Serial aspiration with smart expansion
        window = optimal_window
        
        # [Extension 6] Set pruning parameters
        aspiration_pruning_synergy.update_from_aspiration(
            AspirationState(
                window=window,
                window_confidence=optimal_window.confidence,
                trend=causal.expected_future_volatility if causal else 0,
                score_volatility=np.sqrt(posterior.variance),
                move_stability=search_state.move_stability,
                is_research=False,
            )
        )
        
        for attempt in range(MAX_ATTEMPTS):
            # Search with window
            result = search(
                position, search_state.target_depth,
                window.alpha, window.beta,
                pruning_adjustments=aspiration_pruning_synergy.pruning_adjustments,
            )
            
            # Success?
            if window.alpha < result.score < window.beta:
                break
            
            # Fail handling
            fail_type = 'fail_low' if result.score <= window.alpha \
                        else 'fail_high'
            fail_magnitude = abs(
                result.score - 
                (window.alpha if fail_type == 'fail_low' else window.beta)
            )
            
            # [Extension 1] Update posterior with fail information
            bayesian_model.update_with_fail(
                fail_type=fail_type,
                fail_score=result.score,
                window=window,
            )
            posterior = bayesian_model.compute_posterior()
            
            # [Extension 7] Compute re-search depth
            research_depth = adaptive_research_depth.compute_research_depth(
                original_depth=search_state.target_depth,
                fail_type=fail_type,
                fail_magnitude=fail_magnitude,
                window=window,
                score_analysis=score_analysis,
                time_factor=time_factor,
            )
            
            # Recompute window from updated posterior
            window = window_optimizer.compute_optimal_window(
                posterior, 
                target_confidence=min(target_confidence + 0.05, 0.95),
                time_factor=time_factor,
            )
            
            # [Extension 6] Update pruning for re-search
            aspiration_pruning_synergy.update_from_aspiration(
                AspirationState(
                    window=window,
                    is_research=True,
                    fail_type=fail_type,
                )
            )
            
            # Early termination
            if attempt >= MAX_ATTEMPTS - 1:
                window = Window(alpha=-MATE_VALUE, beta=MATE_VALUE)
                research_depth = search_state.target_depth
            
            # Re-search (possibly at reduced depth)
            result = search(
                position, research_depth,
                window.alpha, window.beta,
                pruning_adjustments=aspiration_pruning_synergy.pruning_adjustments,
            )
            
            if window.alpha < result.score < window.beta:
                # [Extension 7] Verify if needed
                if adaptive_research_depth.should_verify_at_full_depth(
                    result, search_state.target_depth, research_depth
                ):
                    # Quick verification at full depth with tight window
                    verify_window = Window(
                        alpha=result.score - 5,
                        beta=result.score + 5,
                    )
                    verify_result = search(
                        position, search_state.target_depth,
                        verify_window.alpha, verify_window.beta,
                    )
                    
                    if (verify_window.alpha < verify_result.score < 
                        verify_window.beta):
                        result = verify_result
                    # Else: use reduced-depth result anyway
                
                break
    
    # ═══════════════════════════════════════════
    # PHASE 3: POST-SEARCH LEARNING
    # ═══════════════════════════════════════════
    
    # [Extension 1] Update Bayesian model
    bayesian_model.update_with_iteration(
        depth=search_state.iteration,
        score=result.score,
        nodes_searched=result.nodes,
        best_move=result.best_move,
        position_features=position_profile,
    )
    
    # [Extension 5] Update cross-position knowledge
    similarity_engine.update_cluster_knowledge(
        position,
        AspirationResult(
            optimal_delta=abs(result.score - search_state.prev_score),
            had_fail=result.had_fail,
            score_change=abs(result.score - search_state.prev_score),
            expansion_used=result.expansion_factor,
        ),
    )
    
    # [Extension 6] Collect pruning feedback
    aspiration_pruning_synergy.update_from_pruning(
        result.pruning_stats
    )
    
    # [Extension 7] Record depth performance
    if result.research_depth < search_state.target_depth:
        adaptive_research_depth.record_depth_performance(
            original_depth=search_state.target_depth,
            research_depth=result.research_depth,
            was_successful=result.score_within_window,
            nodes_saved=result.nodes_saved_estimate,
        )
    
    # [Extension 2] Store causal information for next iteration
    causal_analyzer.store_result(
        search_state.iteration, result, position
    )
    
    return result
```

---

## XI. Ước Tính Ảnh Hưởng Tổng Hợp AAW-X

```
┌───────────────────────────────────────────┬────────────┬──────────────┐
│ Extension                                 │ Elo Est.   │ Confidence   │
├───────────────────────────────────────────┼────────────┼──────────────┤
│ AAW Base (from original document)         │ +30-60     │ ★★★★ High    │
│                                           │            │              │
│ Ext 1: Bayesian Score Distribution        │ +15-30     │ ★★★★ High    │
│   - Optimal window placement              │ +8-15      │              │
│   - Bimodal detection & handling          │ +5-10      │              │
│   - Skew-aware asymmetric windows         │ +3-8       │              │
│                                           │            │              │
│ Ext 2: Causal Score Analysis              │ +10-20     │ ★★★ Medium   │
│   - Tactical resolution detection         │ +4-8       │              │
│   - Oscillation pattern handling          │ +3-6       │              │
│   - Horizon effect prediction             │ +3-6       │              │
│                                           │            │              │
│ Ext 3: Speculative Parallel Windows       │ +15-35     │ ★★★★ High    │
│   - Multi-window parallel search          │ +10-20     │              │
│   - Shared TT between threads             │ +5-10      │              │
│   - Staggered launch optimization         │ +2-5       │              │
│                                           │            │              │
│ Ext 4: Interior Node Adaptation           │ +8-15      │ ★★★ Medium   │
│   - TT-based window narrowing             │ +5-10      │              │
│   - Depth-selective application           │ +3-5       │              │
│                                           │            │              │
│ Ext 5: Cross-Position Transfer            │ +5-12      │ ★★★ Medium   │
│   - Cluster-based knowledge              │ +3-7       │              │
│   - Cross-game persistence               │ +2-5       │              │
│                                           │            │              │
│ Ext 6: Aspiration-Pruning Synergy        │ +10-20     │ ★★★★ High    │
│   - LMR adjustment                       │ +4-8       │              │
│   - Futility margin adaptation           │ +3-6       │              │
│   - Null move tuning                     │ +2-4       │              │
│   - Singular extension tuning            │ +1-3       │              │
│                                           │            │              │
│ Ext 7: Adaptive Re-Search Depth          │ +8-18      │ ★★★★ High    │
│   - Reduced-depth re-search              │ +5-12      │              │
│   - Selective verification               │ +3-6       │              │
│                                           │            │              │
│ Ext 8: Opponent-Aware Aspiration         │ +3-8       │ ★★ Medium    │
│   - Style-based delta adjustment         │ +2-4       │              │
│   - Strength-based asymmetry             │ +1-3       │              │
│   - Game trend awareness                 │ +1-2       │              │
├───────────────────────────────────────────┼────────────┼──────────────┤
│ Extensions Total (with overlap)           │ +45-95     │              │
│ AAW Base + Extensions                     │ +65-130    │              │
│ After overhead & interaction deduction    │ +50-105    │              │
│ Conservative Estimate                     │ +40-80     │              │
│ Realistic Center Estimate                 │ +55-75     │              │
└───────────────────────────────────────────┴────────────┴──────────────┘

By scenario:
┌──────────────────────────┬────────────┬──────────────────────────────┐
│ Scenario                 │ Elo Gain   │ Key Extensions               │
├──────────────────────────┼────────────┼──────────────────────────────┤
│ Tactical / Sharp         │ +60-100    │ Bayesian bimodal, causal,    │
│                          │            │ parallel, adaptive depth     │
│ Stable / Positional      │ +25-40     │ Tight posterior, pruning     │
│                          │            │ synergy, interior adapt      │
│ Endgame                  │ +30-50     │ Transfer learning, adaptive  │
│                          │            │ depth, tight posterior        │
│ Time Pressure            │ +50-80     │ Parallel windows, adaptive   │
│                          │            │ depth, reduced re-search     │
│ Score Trending            │ +40-65     │ Causal, Bayesian skew,      │
│                          │            │ opponent-aware               │
│ Multi-core (8+ threads)  │ +55-90     │ Parallel windows dominant    │
│ Single-core              │ +35-55     │ Bayesian + causal dominant   │
└──────────────────────────┴────────────┴──────────────────────────────┘
```

---

## XII. Computational Budget — AAW-X

```
┌──────────────────────────────────┬────────────┬──────────────────────┐
│ Component                        │ Time (μs)  │ Amortized Impact     │
├──────────────────────────────────┼────────────┼──────────────────────┤
│ AAW Base overhead                │ 3.0-9.0    │ Per iteration        │
│ Bayesian posterior update        │ 1.0-3.0    │ Per iteration        │
│ Bayesian window optimization     │ 0.5-2.0    │ Per iteration        │
│ Causal analysis                  │ 2.0-5.0    │ Per iteration        │
│ Cross-position lookup            │ 1.0-3.0    │ Per iteration        │
│ Parallel window allocation       │ 0.5-1.0    │ Per iteration        │
│ Interior node adaptation (amort) │ 0.01-0.05  │ Per interior PV node │
│ Pruning synergy update           │ 0.1-0.3    │ Per attempt          │
│ Opponent model query             │ 0.1-0.5    │ Per iteration        │
│ Adaptive depth computation       │ 0.1-0.3    │ Per re-search        │
├──────────────────────────────────┼────────────┼──────────────────────┤
│ TOTAL AAW-X overhead             │ 8.0-24.0   │ Per iteration        │
│ vs search time per iteration     │ ~0.001%    │ Negligible           │
│                                  │            │                      │
│ Savings from reduced re-search   │ -25-50%    │ Per fail event       │
│ Savings from parallel            │ -30-60%    │ Per aspiration cycle  │
│ Savings from adaptive depth      │ -20-40%    │ Per re-search        │
│ Savings from pruning synergy     │ -5-15%     │ Per search           │
├──────────────────────────────────┼────────────┼──────────────────────┤
│ NET search efficiency gain       │ +15-35%    │ Overall              │
│ Memory overhead (Bayesian model) │ ~2 KB      │ Per search           │
│ Memory overhead (clusters)       │ ~1 MB      │ Persistent           │
│ Memory overhead (interior stats) │ ~64 KB     │ Per search           │
└──────────────────────────────────┴────────────┴──────────────────────┘
```

---

## XIII. Lộ Trình Triển Khai Mở Rộng

```
Phase 1 (Foundation) — Month 1-2:
├── AAW Base implementation
├── Extension 1: Bayesian unimodal posterior
├── Extension 7: Basic adaptive re-search depth
└── Target: +25-40 Elo

Phase 2 (Core Intelligence) — Month 3-4:
├── Extension 1: Bimodal detection & handling
├── Extension 2: Causal analysis (tactical resolution, oscillation)
├── Extension 6: Aspiration-Pruning synergy (LMR, futility)
└── Target: +40-60 Elo

Phase 3 (Parallelism) — Month 5-6:
├── Extension 3: Speculative parallel windows
├── Extension 3: Shared TT coordination
├── Extension 3: Staggered launch
└── Target: +50-75 Elo (on multi-core)

Phase 4 (Interior + Transfer) — Month 7-8:
├── Extension 4: Interior node adaptive windows
├── Extension 5: Cross-position similarity clustering
├── Extension 5: Cross-game persistence
└── Target: +55-85 Elo

Phase 5 (Refinement) — Month 9-10:
├── Extension 8: Opponent-aware aspiration
├── Extension 2: Advanced causal (horizon effect)
├── Extension 6: Full pruning synergy (null move, singular)
├── Cross-extension interaction tuning
└── Target: +60-95 Elo

Phase 6 (Optimization) — Month 11-12:
├── SIMD acceleration for Bayesian computation
├── Fast path optimization for stable positions
├── Parameter freeze from self-play
├── Tournament validation
└── Target: +55-80 Elo (production, conservative)
```