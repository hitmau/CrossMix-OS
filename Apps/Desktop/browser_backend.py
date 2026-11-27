#!/usr/bin/env python3
"""Small HTTPS search/page backend for the SDL Desktop browser view.

Protocol (UTF-8, one tab-delimited record per line):
  search QUERY  -> RESULT<TAB>url<TAB>title<TAB>snippet, DONE<TAB>count
  open URL      -> TITLE<TAB>title, TEXT<TAB>line, LINK<TAB>url<TAB>label,
                   DONE<TAB>count
Errors use ERROR<TAB>message and a non-zero exit status.
"""

from __future__ import annotations

import ipaddress
import base64
import sys
from html.parser import HTMLParser
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlencode, urljoin, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


USER_AGENT = "CrossMixDesktop/1.0 (TrimUI; lightweight browser)"
TIMEOUT_SECONDS = 12
MAX_RESPONSE_BYTES = 1024 * 1024
MAX_RESULTS = 12
MAX_PAGE_LINES = 80
MAX_LINKS = 30
MAX_TEXT_CHARS = 6000
VOID_TAGS = {"area", "base", "br", "col", "embed", "hr", "img", "input",
             "link", "meta", "param", "source", "track", "wbr"}


def clean(value: str, limit: int = 500) -> str:
    """Keep the wire protocol single-line and bounded."""
    return " ".join((value or "").replace("\t", " ").replace("\r", " ").split())[:limit]


def validate_url(url: str) -> str:
    """Allow public HTTP(S) hosts; reject local names and literal IP targets."""
    parsed = urlsplit(url.strip())
    if parsed.scheme.lower() not in {"http", "https"} or not parsed.hostname:
        raise ValueError("URL precisa usar http ou https")
    if parsed.username is not None or parsed.password is not None:
        raise ValueError("URL com credenciais nao e aceita")
    host = parsed.hostname.rstrip(".").lower()
    if host in {"localhost", "localhost.localdomain"} or host.endswith(
        (".localhost", ".local", ".lan", ".internal", ".home", ".test")
    ):
        raise ValueError("Endereco local nao e aceito")
    try:
        ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        raise ValueError("Enderecos IP diretos nao sao aceitos")
    try:
        port = parsed.port
    except ValueError as exc:
        raise ValueError("Porta da URL invalida") from exc
    if port not in (None, 80, 443):
        raise ValueError("Somente portas web padrao sao aceitas")
    return url


class PublicRedirectHandler(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        target = urljoin(request.full_url, newurl)
        validate_url(target)
        return super().redirect_request(request, fp, code, message, headers, target)


def fetch(url: str) -> tuple[str, str]:
    url = validate_url(url)
    request = Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "text/html,application/xhtml+xml;q=0.9,*/*;q=0.5",
        "Accept-Language": "pt-BR,pt;q=0.9,en;q=0.7",
    })
    opener = build_opener(PublicRedirectHandler())
    with opener.open(request, timeout=TIMEOUT_SECONDS) as response:
        final_url = validate_url(response.geturl())
        body = response.read(MAX_RESPONSE_BYTES + 1)
        if len(body) > MAX_RESPONSE_BYTES:
            raise ValueError("Pagina excede o limite de 1 MiB")
        charset = response.headers.get_content_charset() or "utf-8"
    try:
        return body.decode(charset, errors="replace"), final_url
    except LookupError:
        return body.decode("utf-8", errors="replace"), final_url


class SearchParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.results: list[dict[str, str]] = []
        self.current: dict[str, str] | None = None
        self.in_title = False
        self.in_snippet = False
        self.title_href = ""
        self.title_parts: list[str] = []
        self.snippet_parts: list[str] = []

    @staticmethod
    def _classes(attrs) -> set[str]:
        return set(dict(attrs).get("class", "").split())

    def handle_starttag(self, tag, attrs):
        classes = self._classes(attrs)
        attr = dict(attrs)
        if tag == "li" and "b_algo" in classes:
            self.current = {"url": "", "title": "", "snippet": ""}
            self.title_parts = []
            self.snippet_parts = []
            self.in_title = False
            self.in_snippet = False
        elif self.current is not None:
            if tag == "h2":
                self.in_title = True
            elif tag == "a" and self.in_title:
                self.title_href = attr.get("href", "")
                self.current["url"] = self.title_href
            elif tag == "p" and "b_lineclamp2" in classes:
                self.in_snippet = True

    def handle_data(self, data):
        if self.current is not None and self.in_title:
            self.title_parts.append(data)
        if self.current is not None and self.in_snippet:
            self.snippet_parts.append(data)

    def handle_endtag(self, tag):
        if tag == "h2" and self.current is not None:
            self.current["title"] = clean("".join(self.title_parts), 180)
            self.in_title = False
        elif tag == "p" and self.current is not None and self.in_snippet:
            self.current["snippet"] = clean("".join(self.snippet_parts), 260)
            self.in_snippet = False
        elif tag == "li" and self.current is not None:
            self._finish_result()

    def _finish_result(self):
        if self.current:
            href = self.current.get("url", "")
            if href:
                href = urljoin("https://www.bing.com/", href)
                try:
                    parts = urlsplit(href)
                    query = parse_qs(parts.query)
                    encoded = query.get("u", [""])[0]
                    if parts.hostname and parts.hostname.endswith("bing.com") and encoded.startswith("a1"):
                        token = encoded[2:]
                        decoded = base64.urlsafe_b64decode(token + "=" * (-len(token) % 4)).decode("utf-8", errors="replace")
                        href = decoded
                    validate_url(href)
                except ValueError:
                    href = ""
                except (base64.binascii.Error, UnicodeError):
                    href = ""
            title = clean(self.current.get("title", ""), 180)
            if href and title:
                self.results.append({
                    "url": href,
                    "title": title,
                    "snippet": clean(self.current.get("snippet", ""), 260),
                })
        self.current = None
        self.in_title = False
        self.in_snippet = False
        self.title_href = ""
        self.title_parts = []
        self.snippet_parts = []


class PageParser(HTMLParser):
    def __init__(self, base_url: str):
        super().__init__(convert_charrefs=True)
        self.base_url = base_url
        self.title_parts: list[str] = []
        self.in_title = False
        self.text_parts: list[str] = []
        self.skip_depth = 0
        self.links: list[tuple[str, str]] = []
        self.anchor_href = ""
        self.anchor_parts: list[str] | None = None

    def handle_starttag(self, tag, attrs):
        attr = dict(attrs)
        if tag in {"script", "style", "noscript", "svg", "template"}:
            self.skip_depth += 1
            return
        if tag == "title":
            self.in_title = True
        if self.skip_depth:
            return
        if tag in {"p", "div", "br", "hr", "li", "h1", "h2", "h3", "h4", "tr"}:
            self.text_parts.append("\n")
        if tag == "a" and attr.get("href") and self.anchor_parts is None:
            self.anchor_href = urljoin(self.base_url, attr["href"])
            self.anchor_parts = []

    def handle_data(self, data):
        if self.in_title:
            self.title_parts.append(data)
        if not self.skip_depth:
            self.text_parts.append(data)
        if self.anchor_parts is not None:
            self.anchor_parts.append(data)

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False
        if tag in {"script", "style", "noscript", "svg", "template"} and self.skip_depth:
            self.skip_depth -= 1
            return
        if not self.skip_depth and tag in {"p", "div", "br", "hr", "li", "h1", "h2", "h3", "h4", "tr"}:
            self.text_parts.append("\n")
        if tag == "a" and self.anchor_parts is not None:
            label = clean("".join(self.anchor_parts), 120)
            href = self.anchor_href
            self.anchor_href = ""
            self.anchor_parts = None
            if label and len(self.links) < MAX_LINKS:
                try:
                    validate_url(href)
                except ValueError:
                    return
                if not any(existing == href for existing, _ in self.links):
                    self.links.append((href, label))


def output_search(query: str) -> None:
    if not query:
        raise ValueError("Digite algo para pesquisar")
    search_url = "https://www.bing.com/search?" + urlencode({"q": query[:240], "setlang": "pt-BR"})
    markup, _ = fetch(search_url)
    parser = SearchParser()
    parser.feed(markup)
    if not parser.results and "b_algo" not in markup:
        raise ValueError("O mecanismo de busca nao devolveu resultados para esta requisicao")
    for item in parser.results[:MAX_RESULTS]:
        print("RESULT\t{}\t{}\t{}".format(
            clean(item["url"], 900), clean(item["title"], 180), clean(item["snippet"], 260)
        ))
    print("DONE\t{}".format(min(len(parser.results), MAX_RESULTS)))


def output_page(url: str) -> None:
    markup, final_url = fetch(url)
    parser = PageParser(final_url)
    parser.feed(markup)
    print("TITLE\t{}".format(clean("".join(parser.title_parts), 180) or final_url))
    text = "".join(parser.text_parts)
    lines = [clean(line, 180) for line in text[:MAX_TEXT_CHARS].splitlines()]
    lines = [line for line in lines if line][:MAX_PAGE_LINES]
    for line in lines:
        print("TEXT\t{}".format(line))
    for href, label in parser.links:
        print("LINK\t{}\t{}".format(clean(href, 900), clean(label, 120)))
    print("DONE\t{}".format(len(parser.links)))


def main(argv: list[str]) -> int:
    try:
        if not argv:
            raise ValueError("uso: browser_backend.py search QUERY | open URL")
        action = argv[0].lower()
        if action == "search":
            output_search(" ".join(argv[1:]).strip())
        elif action == "open" and len(argv) == 2:
            output_page(argv[1])
        else:
            raise ValueError("uso: browser_backend.py search QUERY | open URL")
        return 0
    except (HTTPError, URLError, TimeoutError, OSError, ValueError) as exc:
        message = clean(str(exc), 220) or "falha de rede"
        print("ERROR\t{}".format(message))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
