#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""docx2md.py —— 把 .docx 转成 Markdown（本工程文档用）。

只做只读转换：读 docx 的 word/document.xml，输出 md 到 stdout。
支持：标题（Heading1-4 / 中文样式名）、列表、表格、加粗、斜体、行内代码、
      段落、空段、下划线编号（w:numPr 暂时按普通段落处理）。
不依赖第三方库（只 stdlib：zipfile + xml.etree）。
"""
import sys
import zipfile
import xml.etree.ElementTree as ET

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"


def q(tag):
    return W + tag


def para_style(p):
    pPr = p.find(q("pPr"))
    if pPr is None:
        return ""
    st = pPr.find(q("pStyle"))
    if st is None:
        return ""
    return st.get(q("val")) or ""


def is_list(p):
    pPr = p.find(q("pPr"))
    return pPr is not None and pPr.find(q("numPr")) is not None


def heading_level(style):
    """把 Word 样式名映射到 markdown 标题级别；0 = 不是标题。"""
    if not style:
        return 0
    s = style.lower()
    for n in (6, 5, 4, 3, 2, 1):
        if s == "heading%d" % n or s == "heading %d" % n:
            return n
    # 中文样式名（Word 中文版）："标题 1"
    for n in (1, 2, 3, 4, 5, 6):
        if style.strip() in ("标题 %d" % n, "标题%d" % n):
            return n
    if s in ("title",):
        return 1
    if s in ("subtitle",):
        return 2
    return 0


def run_text(r):
    """一个 run 的文本（含 w:t 与 w:tab / w:br）。"""
    out = []
    for ch in r:
        t = ch.tag
        if t == q("t"):
            out.append(ch.text or "")
        elif t == q("tab"):
            out.append("\t")
        elif t in (q("br"), q("cr")):
            out.append("\n")
    return "".join(out)


def run_md(r):
    txt = run_text(r)
    if not txt:
        return ""
    rPr = r.find(q("rPr"))
    bold = italic = code = False
    if rPr is not None:
        bold = rPr.find(q("b")) is not None
        italic = rPr.find(q("i")) is not None
        # 等宽字体（Consolas 等）近似当作行内代码
        rf = rPr.find(q("rFonts"))
        if rf is not None:
            f = (rf.get(q("ascii")) or "") + (rf.get(q("eastAsia")) or "")
            if "consol" in f.lower() or "courier" in f.lower():
                code = True
    # 去掉首尾空白后再包裹标记，避免 " ** x ** "
    lead = txt[: len(txt) - len(txt.lstrip())]
    trail = txt[len(txt.rstrip()):]
    core = txt.strip()
    if not core:
        return txt
    if code:
        core = "`" + core + "`"
    if bold:
        core = "**" + core + "**"
    if italic:
        core = "*" + core + "*"
    return lead + core + trail


def para_text(p, in_table=False):
    parts = []
    for r in p:
        if r.tag == q("r"):
            parts.append(run_md(r))
        elif r.tag == q("hyperlink"):
            for rr in r:
                if rr.tag == q("r"):
                    parts.append(run_md(rr))
    s = "".join(parts)
    # 表格里把换行压成 <br>，否则 markdown 表格会断
    if in_table:
        s = s.replace("\n", "<br>")
    return s.rstrip()


def table_md(tbl):
    rows = []
    for tr in tbl.findall(q("tr")):
        cells = []
        for tc in tr.findall(q("tc")):
            ps = [para_text(p, True) for p in tc.findall(q("p"))]
            cells.append(" ".join([x for x in ps if x is not None]).strip())
        rows.append(cells)
    if not rows:
        return []
    out = []
    width = max(len(r) for r in rows)
    rows = [r + [""] * (width - len(r)) for r in rows]
    out.append("| " + " | ".join(rows[0]) + " |")
    out.append("|" + "|".join(["---"] * width) + "|")
    for r in rows[1:]:
        out.append("| " + " | ".join(r) + " |")
    return out


def convert(path):
    z = zipfile.ZipFile(path)
    root = ET.fromstring(z.read("word/document.xml"))
    body = root.find(q("body"))
    lines = []
    for el in body:
        if el.tag == q("p"):
            style = para_style(el)
            lvl = heading_level(style)
            txt = para_text(el)
            if lvl:
                if txt:
                    lines.append("#" * lvl + " " + txt)
                    lines.append("")
                continue
            if not txt.strip():
                # 连续空段压成一个
                if lines and lines[-1] != "":
                    lines.append("")
                continue
            if is_list(el):
                lines.append("- " + txt.strip())
            else:
                lines.append(txt)
        elif el.tag == q("tbl"):
            lines.extend(table_md(el))
            lines.append("")
    # 压缩连续空行
    out = []
    for ln in lines:
        if ln == "" and out and out[-1] == "":
            continue
        out.append(ln)
    return "\n".join(out).rstrip() + "\n"


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    print(convert(sys.argv[1]), end="")
