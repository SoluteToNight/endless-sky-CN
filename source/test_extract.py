import re
import glob

files = glob.glob('e:/CODE/endless-sky/source/*Panel.cpp')
out_strings = set()

# Special categories
special_words = [
    # Preferences strings pattern: const string SOMETHING = "Value";
]

for f in files:
    with open(f, 'r', encoding='utf-8', errors='ignore') as file:
        content = file.read()
        
        # very broad regex: string literals with at least a space or uppercase, mostly alphabetic
        matches = re.findall(r'"([A-Za-z][A-Za-z0-9\s()/\-:,%.!?]{2,})"', content)
        for m in matches:
            if ' ' in m or any(c.isupper() for c in m):
                # Filter out obvious non-UI strings (some filenames, etc)
                if not m.endswith(".txt") and not m.endswith(".png") and not m.endswith(".wav"):
                    out_strings.add(m)

        # Explicit table.Draw calls
        matches = re.findall(r'(?:table\.Draw|emplace_back)\s*\(\s*"([^"]+)"', content)
        for m in matches:
            out_strings.add(m)

# Filter words
valid_out = []
for s in sorted(out_strings):
    # try to keep only strings that look like english text
    if any(c.isalpha() for c in s) and len(s) > 2:
        valid_out.append(s)

with open('e:/CODE/endless-sky/source/extracted_strings.txt', 'w', encoding='utf-8') as out:
    for s in valid_out:
        out.write(s + "\n")
