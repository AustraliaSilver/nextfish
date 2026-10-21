# HARENN training corpus

The generated training corpus is **not** stored in this branch. It is produced by
`.github/workflows/datagen.yml` and pushed to the `data` branch, which keeps the
main branch free of several hundred megabytes of generated JSONL.

To fetch it:

```bash
git fetch origin data:data
git checkout data -- data/
```

`train_harenn.py` reads the corpus from `data/` (`*.json` and `*.jsonl`).
