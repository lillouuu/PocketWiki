import wikipediaapi
import os
import requests
import time
import re
from pathlib import Path
from openai import OpenAI
client =OpenAI(
    api_key=os.environ.get("POCKET_WIKI_API_KEY"),
    base_url="https://api.x.ai/v1"
)
responseg = client.chat.completions.create(
    model="grok-3",
    messages=[
        {"role": "user", "content":"Generate a list of 50 broad Wikipedia category names "
                                    "covering science, history, mathematics, geography, technology"
                                    ", philosophy, nature, art and literature. Return only the category names "
                                    "one per line, no numbering, no explanation. Use exact Wikipedia"
                                    " category names"}
    ]
)
topics_text = responseg.choices[0].message.content
categories = [line.strip() for line in topics_text.splitlines() if line.strip()]


def clean_text(text):
    markers = ["== See also ==", "== References ==", "== External links =="]
    for marker in markers:
        if marker in text:
            text = text.split(marker)[0]

    text = text.replace('"', '"').replace('"', '"').replace("'", "'")

    text = re.sub(r'\n{3,}', '\n\n', text)

    return text.strip()

def sanitize_filename(name):
    """
    Cleans the filename for the .txt file itsealf .
    """
    return re.sub(r'[<>:"/\\|?*]', '', name).strip()

def get_storage_path(root,title):
    clean_path=[]
    for char in title :
        if char.isalpha():
            clean_path.append(char.lower())
        elif char.isdigit():
            clean_path.append("#")
    clean_path = clean_path[:10] # max depth for folder nesting
    return Path(root).joinpath(*clean_path)


wiki = wikipediaapi.Wikipedia(user_agent="PocketWiki/1.0", language="en")
output_folder = "articles"
os.makedirs(output_folder, exist_ok=True)
url = "https://wikimedia.org/api/rest_v1/metrics/pageviews/top/en.wikipedia/all-access/2024/01/01"
headers = {"User-Agent": "PocketWiki/1.0"}
response = requests.get(url, headers=headers)
data = response.json()
top_articles = [article["article"] for article in data["items"][0]["articles"][:10]]
for title in top_articles:
    try :
        page = wiki.page(title)
        if page.exists():
            content= clean_text(page.text)
        if not content:
            print(f"skipped (empty): {title}")
            continue
        clean_title = sanitize_filename(title)
        folder_path = get_storage_path(output_folder, title)
        file_path = folder_path / (clean_title + ".txt")
        if file_path.exists():
            print(f"skipped (already exists): {clean_title}")
            continue
        folder_path.mkdir(parents=True, exist_ok=True)
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
        time.sleep(1)
    except Exception as e :
        print(f"error on {title}: {e}")

