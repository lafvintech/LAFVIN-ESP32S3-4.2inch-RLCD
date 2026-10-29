(() => {
  // Theme and raw HTML links are outside the Sphinx document tree.
  function openExternalLinksInNewTabs() {
    document.querySelectorAll("a[href]").forEach((link) => {
      if (
        (link.protocol === "http:" || link.protocol === "https:") &&
        link.origin !== window.location.origin
      ) {
        link.target = "_blank";
        link.relList.add("noopener", "noreferrer");
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", openExternalLinksInNewTabs);
  } else {
    openExternalLinksInNewTabs();
  }
})();
