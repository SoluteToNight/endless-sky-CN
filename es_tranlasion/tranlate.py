import os
import re
import json
import hashlib
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from openai import OpenAI
from dotenv import load_dotenv

load_dotenv()

client = OpenAI(api_key=os.getenv("DEEPSEEK_API_KEY"), base_url="https://api.deepseek.com")
MODEL_NAME = "deepseek-chat"
CACHE_FILE = "translation_cache_v5.json"

# --- 根据 Wiki 描述修正的术语表 ---
GLOSSARY = {
    # 基础与人类势力 (Human Factions)
    "Outfit": "装备", "Hull": "船体", "Shield": "护盾", 
    "Republic": "共和国", 
    "Syndicate": "辛迪加", 
    "Free Worlds": "自由世界",
    "Merchants": "商贸团",
    "Pirates": "海盗",
    "Remnant": "遗民",          # 居住在灰烬荒原的高技术人类派系
    "The Deep": "深空域",        # 远离核心区的偏远人类星域
    
    # 人类基因工程产物 (Engineered Humans)
    "Alphas": "阿尔法",          # 基因改造的超级士兵
    "Betas": "贝塔",            # 基因改造的劳工阶层
    
    # 军事与情报机构 (Organizations)
    "Navy": "海军",              # 通常指共和国海军
    "Navy Intelligence": "海军情报局",
    "Republic Intelligence": "共和国情报局",
    "Deep Security": "深空安保",
    "Southern Mutual Defense Pact": "南方互保公约", # 自由世界的前身联盟
    
    # 地理与历史术语 (Lore & Regions)
    "Ember Waste": "灰烬荒原",   # 遗民所在地
    "Graveyard": "坟场",         # 位于银河西南部的危险区域
    "Tangled Shroud": "纠缠星云", # 传承者所在的区域
    "Unification War": "统一战争",
    "Quantum Keystone": "量子基石", # 穿越不稳定虫洞的关键设备
    
    # 核心外星文明 (Major Species)
    "Hai": "亥族",
    "Korath": "科拉特",
    "Kor Mereti": "科·梅雷蒂",
    "Kor Sestor": "科·塞斯托",
    "Wanderer": "漫游者",
    "Coalition": "联合体",
    "Heliarchy": "日政司",
    "Quarg": "夸格",
    "Pug": "帕格",
    "Drak": "德拉克",
    
    # 传承者相关 (Successors Lore)
    "Successors": "传承者",
    "Predecessors": "前代者",
    "High Houses": "高庭世家",
    "Old Houses": "旧世家",
    "New Houses": "新世家",
    "People's Houses": "庶民院",
    
    # 古代与新兴势力 (Ancient & Minor)
    "Rulei": "鲁雷",
    "Precursors": "先行者",
    "Solemnity": "肃穆号",
    "Umbral Reach": "暗影领域",
    "Sheragi": "舍拉吉",
    "Gegno": "杰格诺",
    "Ka'het": "卡希特",
    "Aberrant": "异端",
    "Bunrodea": "邦罗迪亚",
    "Incipias": "因希比亚",
    "Avgi": "阿维吉"
}

TARGET_TAGS = [
    "display name", "description", "name", "spaceport", "dialog",
    "choice", "conversation", "landing message", "tip", "help"
]
CREATIVE_BLOCKS = {"conversation", "dialog", "choice", "tip", "help"}

class RobustTranslator:
    def __init__(self):
        self.cache = self.load_cache()
        self.cache_lock = threading.Lock()
        self.stats = {"reused": 0, "new_translated": 0, "errors": 0, "total_requests": 0}
        self.start_time = None
        self.is_running = False

        self.logic_pattern = re.compile(r'(<[^>]+>|&\[[^\]]+\])')
        tags_pattern = "|".join([re.escape(t) for t in TARGET_TAGS])
        # 匹配单行 tag "content"
        self.tag_regex = re.compile(rf'^(\s*)({tags_pattern})\s+([\"`])(.+)([\"`])\s*$')
        # 匹配缩进的多行内容 `content`
        self.multi_line_regex = re.compile(r'^(\s+)([\"`])(.+)([\"`])\s*$')
        # 匹配块级关键字（如 conversation / variant / fleet）
        self.block_regex = re.compile(r'^([A-Za-z_][\w-]*)\b')

    def load_cache(self):
        if os.path.exists(CACHE_FILE):
            try:
                with open(CACHE_FILE, 'r', encoding='utf-8') as f: return json.load(f)
            except: return {}
        return {}

    def save_cache(self):
        with open(CACHE_FILE, 'w', encoding='utf-8') as f:
            json.dump(self.cache, f, ensure_ascii=False, indent=2)

    def _periodic_report(self):
        while self.is_running:
            time.sleep(10)
            if not self.is_running:
                break
            elapsed = time.time() - self.start_time
            with self.cache_lock:
                req_count = self.stats["total_requests"]
                speed = req_count / elapsed if elapsed > 0 else 0
                print(
                    f"\n[监控汇报] 运行:{int(elapsed)}s | 速度:{speed:.2f} req/s | "
                    f"已汉化:{self.stats['new_translated']}"
                )

    def _matched_glossary(self, text):
        matched = {}
        for eng, chs in GLOSSARY.items():
            if re.search(re.escape(eng), text, re.IGNORECASE):
                matched[eng] = chs
        return matched

    def translate_via_api(self, text, tag):
        if not text.strip(): return text
        with self.cache_lock:
            self.stats["total_requests"] += 1

        # 1. 占位符脱敏
        placeholders = self.logic_pattern.findall(text)
        temp_text = text
        for i, p in enumerate(placeholders):
            temp_text = temp_text.replace(p, f" %ID_{i}% ", 1)

        # 2. 仅传入当前文本命中的术语，降低提示噪声
        matched_glossary = self._matched_glossary(text)
        glossary_str = ", ".join([f"{k}->{v}" for k, v in matched_glossary.items()]) if matched_glossary else "（无命中术语）"
        try:
            tag_for_prompt = tag if tag else "文本"
            prompt = (
                f"你是一个专业的科幻游戏汉化专家。请翻译《Endless Sky》中的{tag_for_prompt}内容。\n"
                f"【术语库】：{glossary_str}\n"
                "【极其重要】：严禁翻译或修改 %ID_N% 标记，它们代表代码。请保持其在句子中的语义位置。\n"
                f"待翻译文本: {temp_text}"
            )
            response = client.chat.completions.create(
                model=MODEL_NAME,
                messages=[{"role": "system", "content": "你是一名精通太空歌剧风格的翻译官，翻译需严谨且富有科幻感。"},
                          {"role": "user", "content": prompt}],
                temperature=0.1
            )
            res = response.choices[0].message.content.strip().strip('"').strip('`')

            # 3. 术语库校正（只针对当前命中的术语）
            for eng, chs in matched_glossary.items():
                res = re.compile(re.escape(eng), re.IGNORECASE).sub(chs, res)

            # 4. 强力占位符还原
            for i, p in enumerate(placeholders):
                pattern = rf'%\s*(?:ID|id|Id)\s*_{{0,1}}\s*{i}\s*%'
                res = re.sub(pattern, p, res)
            return re.sub(r'\s{2,}', ' ', res).strip()
        except Exception:
            with self.cache_lock:
                self.stats["errors"] += 1
            return None

    def _handle_logic(self, indent, tag, quote, content, eq, is_creative):
        if len(content.strip()) < 2:
            return f'{indent}{tag + " " if tag else ""}{quote}{content}{eq}\n'

        h = hashlib.md5(content.encode()).hexdigest()
        tag_prefix = f"{tag} " if tag else ""
        if h in self.cache:
            with self.cache_lock:
                self.stats["reused"] += 1
            return f'{indent}{tag_prefix}{quote}{self.cache[h]}{eq}\n'

        res = self.translate_via_api(content, tag if tag else "text")
        if res:
            with self.cache_lock:
                self.cache[h] = res
                self.save_cache()
                self.stats["new_translated"] += 1
            return f'{indent}{tag_prefix}{quote}{res}{eq}\n'
        return f'{indent}{tag_prefix}{quote}{content}{eq}\n'

    def process_line(self, line_data):
        line, is_creative = line_data
        if "phrase" in line:
            return line

        # 尝试匹配单行标签格式
        m = self.tag_regex.match(line)
        if m:
            indent, tag, quote, content, eq = m.groups()
            return self._handle_logic(indent, tag, quote, content, eq, is_creative)

        # 尝试匹配缩进内容格式（适用于 tip 下方的反引号文本）
        m_multi = self.multi_line_regex.match(line)
        if m_multi:
            indent, quote, content, eq = m_multi.groups()
            # 仅翻译 creative 语境下的反引号文本，避免误翻译 ship/system/outfit ID。
            if is_creative and quote == "`":
                return self._handle_logic(indent, "", quote, content, eq, is_creative)
        return line

    def run(self, in_p, out_p):
        self.start_time = time.time()
        self.is_running = True
        threading.Thread(target=self._periodic_report, daemon=True).start()

        try:
            tasks = []
            if os.path.isfile(in_p):
                tasks.append((in_p, out_p))
            else:
                for root, _, files in os.walk(in_p):
                    for file in files:
                        if file.endswith(".txt"):
                            inf = os.path.join(root, file)
                            outf = os.path.join(out_p, os.path.relpath(inf, in_p))
                            tasks.append((inf, outf))

            for inf, outf in tasks:
                os.makedirs(os.path.dirname(outf), exist_ok=True)
                with open(inf, 'r', encoding='utf-8') as f:
                    lines = f.readlines()

                line_contexts, active_indents = [], []
                for line in lines:
                    stripped = line.lstrip('\t ')
                    curr_indent = len(line) - len(stripped)
                    while active_indents and curr_indent <= active_indents[-1]:
                        if not stripped:
                            break
                        active_indents.pop()
                    is_creative = len(active_indents) > 0
                    line_contexts.append((line, is_creative))
                    # 仅将明确文本块标记为 creative 语境。
                    block_match = self.block_regex.match(stripped)
                    if block_match and block_match.group(1) in CREATIVE_BLOCKS:
                        active_indents.append(curr_indent)

                with ThreadPoolExecutor(max_workers=6) as exe:
                    results = list(exe.map(self.process_line, line_contexts))

                with open(outf, 'w', encoding='utf-8') as f:
                    f.writelines(results)
        finally:
            self.is_running = False
            total = time.time() - self.start_time
            speed = self.stats["total_requests"] / total if total > 0 else 0
            print(f"\n任务结束 | 耗时:{total:.1f}s | 速度:{speed:.2f}req/s")

if __name__ == "__main__":
    translator = RobustTranslator()
    translator.run("data", "zh_cn")
