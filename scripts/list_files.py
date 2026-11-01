import urllib.request
import re

url = 'https://huggingface.co/buckets/australia1/myrandombucket'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req, timeout=10) as resp:
        html = resp.read().decode('utf-8', errors='ignore')
        # find href="/buckets/australia1/myrandombucket/resolve/..."
        resolves = re.findall(r'/buckets/australia1/myrandombucket/resolve/([^"?#]+)', html)
        unique_files = sorted(list(set(resolves)))
        print(f"Total unique jsonl files found: {len(unique_files)}")
        for f in unique_files:
            print(f)
except Exception as e:
    print("Error:", e)
