import urllib.request
import json

url = 'https://huggingface.co/api/buckets/australia1/myrandombucket'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.loads(resp.read().decode('utf-8'))
        files = data if isinstance(data, list) else data.get('objects', data.get('files', []))
        print(f"Total items returned: {len(files)}")
        total_size = sum(item.get('size', 0) for item in files)
        print(f"Total size: {total_size / (1024*1024):.2f} MB ({total_size / (1024*1024*1024):.2f} GB)")
        for item in files[:15]:
            print(f"{item.get('path')}: {item.get('size', 0) / (1024*1024):.2f} MB")
except Exception as e:
    print('Error:', e)
