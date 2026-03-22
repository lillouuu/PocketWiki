import wikipediaapi
import os
import time
import re
import unicodedata
from pathlib import Path
from google import genai
from dotenv import load_dotenv
load_dotenv()

def clean_text(text):
    if not text:
        return ""

    # ── 1. TRUNCATE AT TERMINAL SECTIONS ─────────────────────────────────────
    # Stop before sections that are pure metadata / not useful offline
    terminal_sections = [
        "== See also ==", "== References ==", "== External links ==",
        "== Further reading ==", "== Notes ==", "== Citations ==",
        "== Bibliography ==", "== Footnotes ==", "== Sources =="
    ]
    for marker in terminal_sections:
        if marker in text:
            text = text.split(marker)[0]

    # ── 2. REMOVE INLINE WIKI MARKUP ─────────────────────────────────────────
    # Remove {{template blocks}} — can be nested, so loop until stable
    prev = None
    while prev != text:
        prev = text
        text = re.sub(r'\{\{[^{}]*\}\}', '', text)

    # Remove [[File:...]] and [[Image:...]] embeds
    text = re.sub(r'\[\[(File|Image|Media):[^\]]*\]\]', '', text, flags=re.IGNORECASE)

    # Unwrap [[link|display]] → display text only
    text = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]+)\]\]', r'\1', text)

    # Remove remaining bare [http... links]
    text = re.sub(r'\[https?://\S+(?:\s[^\]]+)?\]', '', text)

    # Remove HTML tags
    text = re.sub(r'<[^>]+>', '', text)

    # ── 3. REMOVE EMPTY / USELESS SECTION HEADERS ────────────────────────────
    # A section with no content after it is just noise
    text = re.sub(r'(==+[^=]+==+)\s*(?=\n==|\Z)', '', text)

    # ── 4. UNICODE & CHARACTER NORMALIZATION ──────────────────────────────────
    import unicodedata
    text = unicodedata.normalize("NFKC", text)          # normalize Unicode forms

    # Curly quotes → straight
    text = text.replace('\u201c', '"').replace('\u201d', '"')
    text = text.replace('\u2018', "'").replace('\u2019', "'")

    # Common typographic symbols → ASCII equivalents
    text = text.replace('\u2013', '-').replace('\u2014', '-')   # en/em dash
    text = text.replace('\u2026', '...')                        # ellipsis
    text = text.replace('\u00b7', '-')                          # middle dot
    text = text.replace('\u2022', '-')                          # bullet
    text = text.replace('\u00a0', ' ')                          # non-breaking space

    # ── 5. REMOVE LEFTOVER JUNK LINES ────────────────────────────────────────
    cleaned_lines = []
    for line in text.splitlines():
        stripped = line.strip()

        # Skip pure punctuation / symbol-only lines
        if re.match(r'^[|\-=\s{}\[\]\\/*#:;]+$', stripped):
            continue

        # Skip lines that are just a number (table artefacts)
        if re.match(r'^\d+$', stripped):
            continue

        # Skip very short orphan lines (1–2 chars, likely markup debris)
        if len(stripped) <= 2:
            continue

        cleaned_lines.append(line)

    text = "\n".join(cleaned_lines)

    # ── 6. WHITESPACE CLEANUP ────────────────────────────────────────────────
    text = re.sub(r'[ \t]+', ' ', text)          # collapse inline spaces/tabs
    text = re.sub(r'\n{3,}', '\n\n', text)       # max 1 blank line between paragraphs

    return text.strip()


def sanitize_filename(name):
    return re.sub(r'[<>:"/\\|?*]', '', name).strip()


def get_storage_path(root, title):
    clean_path = []
    for char in title:
        if char.isalpha():
            clean_path.append(char.lower())
        elif char.isdigit():
            clean_path.append("#")
    clean_path = clean_path[:10]
    return Path(root).joinpath(*clean_path)

client_gemini = genai.Client(api_key=os.environ.get("GEMINI_API_KEY"))

response = client_gemini.models.generate_content(
    model="gemini-2.5-flash",
    contents=(
        "You are a data generator.\n"
        "Generate exactly 60 valid Wikipedia category names.\n\n"

        "Requirements:\n"
        "- Categories must be broad and well-known (not niche or overly specific).\n"
        "- Cover major domains: science, history, mathematics, geography, technology, "
        "philosophy, nature, art, and literature.\n"
        "- Use ONLY official Wikipedia category names (no invented or fictional names).\n"
        "- Avoid duplicates and very similar categories.\n\n"

        "Output rules:\n"
        "- Output EXACTLY 60 lines.\n"
        "- One category name per line.\n"
        "- No numbering, no bullets, no explanations, no extra text.\n"
        "- Output ONLY the category names.\n"
    )
)

topics_text = response.text
categories = [line.strip() for line in topics_text.splitlines() if line.strip()]
print(f"Gemini generated {len(categories)} categories")


wiki = wikipediaapi.Wikipedia(user_agent="PocketWiki/1.0", language="en")
output_folder = "articles"
os.makedirs(output_folder, exist_ok=True)

all_articles = []
for category_name in categories:
    cat_page = wiki.page(f"Category:{category_name}")
    count = 0  # reset for each category
    for title, page in cat_page.categorymembers.items():
        if page.namespace == 0:
            all_articles.append(title)
            count += 1
            if count >= 100:
                break
    print(f"✓ {category_name} → {count} articles")

all_articles = list(set(all_articles))
print(f"found {len(all_articles)} articles total")
print("-" * 40)

for title in all_articles:
    try:
        page = wiki.page(title)
        if page.exists():
            content = clean_text(page.text)
            if not content:
                print(f"skipped (empty): {title}")
                continue
            clean_title = sanitize_filename(title)
            folder_path = get_storage_path(output_folder, title)
            file_path = folder_path / (clean_title + ".txt")
            if file_path.exists():
                print(f"skipped (exists): {clean_title}")
                continue
            folder_path.mkdir(parents=True, exist_ok=True)
            with open(file_path, "w", encoding="utf-8") as f:
                f.write(content)
            time.sleep(0.2)
            print(f"✓ {clean_title}")
    except Exception as e:
        print(f"error on {title}: {e}")