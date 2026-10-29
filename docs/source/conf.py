# Configuration file for the Sphinx documentation builder.

from pygments.lexers import Python3Lexer
from docutils import nodes
import time

project = "LAFVIN ESP32-S3 4.2inch RLCD Development Board"
copyright = (
    f"{time.localtime().tm_year}, LAFVIN"
)
author = "LAFVIN"
source_encoding = "utf-8"
language = "en"

extensions = [
    "myst_parser",
    "sphinx_copybutton",
    "sphinxcontrib.video",
    "sphinxcontrib.images",
]

images_config = {
    "override_image_directive": True,
    "cache_path": "_images",
    "default_image_width": "100%",
    "default_image_height": "auto",
    "default_show_title": False,
    "download": True,
}

pygments_lexers = {
    "python-repl": Python3Lexer(),
}

source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

templates_path = ["_templates"]
exclude_patterns = []

html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_js_files = ["external-links.js"]

html_logo = "_static/lafvin-logo.png"

html_theme_options = {
    "logo_only": True,
}

html_title = project


def open_external_links_in_new_tab(app, doctree, docname):
    """Open web references separately while preserving chapter navigation."""
    if app.builder.format != "html":
        return

    for reference in doctree.findall(nodes.reference):
        uri = reference.get("refuri", "")
        if reference.get("internal") or not uri.startswith(("https://", "http://")):
            continue
        reference["target"] = "_blank"
        rel = set(reference.get("rel", "").split())
        reference["rel"] = " ".join(sorted(rel | {"noopener", "noreferrer"}))


def setup(app):
    app.connect("doctree-resolved", open_external_links_in_new_tab)
