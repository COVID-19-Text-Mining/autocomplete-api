import re
import ijson
from tqdm import tqdm

f = open(r"rsc/keywords_raw.json", encoding="utf-8")

keywords_raw = {}

for kwd in tqdm(ijson.items(f, "item.keywords_ML.item")):
    kwd = re.sub(r"[^A-Za-z\d \-].+", "", kwd)
    kwd_list = kwd.split(" ")
    new_kwd_list = []
    for each in kwd_list:
        if each in ("a", "an", "the", "was", "is", "are", "were", "has", "our", "we", "you") or not each:
            continue
        new_kwd_list.append(each)
    kwd = " ".join(new_kwd_list)
    if not (2 <= len(kwd) <= 50):
        continue
    if re.fullmatch(r"(\S\s){2,}(\S\s?)", kwd) is not None:
        kwd = re.sub(r"\s'", "", kwd)
    keywords_raw.setdefault(kwd, 0)
    keywords_raw[kwd] += 1

f.close()

with open(r"rsc/keywords.tsv", "w", encoding="utf-8") as g:
    for keyword, count in keywords_raw.items():
        g.write(f"{count}\t{keyword}\n")
