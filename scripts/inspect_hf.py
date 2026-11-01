import urllib.request
import re

url = 'https://huggingface.co/buckets/australia1/myrandombucket'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req, timeout=10) as resp:
        html = resp.read().decode('utf-8', errors='ignore')
        files = re.findall(r'href="([^"]+)"', html)
        matched = [f for f in files if any(ext in f.lower() for ext in ['.bin', '.plain', '.zst', '.parquet', '.tar', '.zip', '.csv', '.epd', 'resolve', 'blob'])]
        print("Matched links:")
        for m in set(matched):
            print(m)
        print("\nAll bucket links:")
        for m in set([f for f in files if 'australia1' in f]):
            print(m)
except Exception as e:
    print("Error:", e)
