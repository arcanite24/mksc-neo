// Lists the newest published release's files from the GitHub API.
(() => {
  const box = document.getElementById('downloads-list');
  if (!box) return;
  const repoUrl = box.dataset.repo;
  const repo = repoUrl.replace('https://github.com/', '');
  const PLATFORMS = [
    ['macos', 'macOS'], ['linux', 'Linux'], ['windows', 'Windows'], ['android', 'Android'], ['web', 'Web'],
  ];
  const el = (tag, attrs = {}, ...children) => {
    const node = document.createElement(tag);
    Object.assign(node, attrs);
    node.append(...children);
    return node;
  };
  const size = bytes => bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : Math.ceil(bytes / 1024) + ' KB';
  const fallback = message => box.replaceChildren(el('p', {}, message + ' ',
    el('a', {href: repoUrl + '/releases'}, 'Releases on GitHub'), '.'));

  fetch(`https://api.github.com/repos/${repo}/releases?per_page=10`, {headers: {Accept: 'application/vnd.github+json'}})
    .then(response => { if (!response.ok) throw new Error(response.status); return response.json(); })
    .then(releases => {
      const release = releases.find(r => !r.draft);
      if (!release) return fallback('No release has been published yet. Play in the browser above, build from source, or check');
      const rows = [];
      for (const [key, label] of PLATFORMS) {
        for (const asset of release.assets.filter(a => a.name.includes(`-${key}-`))) {
          const arch = asset.name.replace(/\.(zip|apk|tar\.gz)$/, '').split(`-${key}-`)[1] || '';
          rows.push(el('tr', {},
            el('td', {}, label), el('td', {}, arch),
            el('td', {}, el('a', {href: asset.browser_download_url}, asset.name)),
            el('td', {className: 'num'}, size(asset.size))));
        }
      }
      const sums = release.assets.find(a => a.name === 'SHA256SUMS.txt');
      const date = new Date(release.published_at).toISOString().slice(0, 10);
      box.replaceChildren(
        el('p', {}, el('strong', {}, release.name || release.tag_name), ` (${release.tag_name}, ${date}`,
          release.prerelease ? ', prerelease' : '', '). ',
          el('a', {href: release.html_url}, 'Release notes'),
          ...(sums ? [' · ', el('a', {href: sums.browser_download_url}, 'SHA256SUMS.txt')] : [])),
        el('div', {className: 'scroll'}, el('table', {},
          el('thead', {}, el('tr', {}, el('th', {}, 'Platform'), el('th', {}, 'Arch'), el('th', {}, 'File'),
            el('th', {className: 'num'}, 'Size'))),
          el('tbody', {}, ...rows))));
    })
    .catch(() => fallback('Could not load the release list. See'));
})();
