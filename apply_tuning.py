import json
import re
import sys

def apply_tuning(cpp_file="src/nextfish_strategy.cpp", json_file="tuned_params.json"):
    try:
        with open(json_file, 'r') as f:
            params = json.load(f)
    except FileNotFoundError:
        print("No tuning results found.")
        return

    with open(cpp_file, 'r') as f:
        content = f.read()

    print("Updating C++ source code with tuned values...")
    
    for key, value in params.items():
        # Regex tìm: double KeyName = ...;
        # Thay thế bằng: double KeyName = NewValue;
        pattern = r"(double\s+" + re.escape(key) + r"\s*=\s*)([-+]?[0-9]*\.?[0-9]+)(\s*;)"
        
        # Format số double 2 chữ số thập phân
        new_str = f"\g<1>{value:.2f}\g<3>"
        
        content, count = re.subn(pattern, new_str, content)
        if count > 0:
            print(f"  Updated {key} -> {value:.2f}")
        else:
            print(f"  Warning: Could not find parameter '{key}' in cpp file.")

    with open(cpp_file, 'w') as f:
        f.write(content)
    print("Source code updated successfully.")

if __name__ == "__main__":
    apply_tuning()
