(() => {
  'use strict';
  const data = window.CYUE_SITE;
  if (!data) return;
  const languages = ['en', 'fr'];
  const query = new URL(location.href).searchParams.get('lang');
  let saved = '';
  try { saved = localStorage.getItem('cyuediff.site.language') || ''; } catch (_) {}
  let language = languages.includes(query) ? query : languages.includes(saved) ? saved : 'en';
  const text = key => data.translations[language][key] || data.translations.en[key] || key;
  const dialog = document.getElementById('lightbox');
  const enlarged = document.getElementById('lightbox-image');
  const caption = document.getElementById('lightbox-caption');
  let selectedShot = null;
  let gallery = [];
  let copiedTimer;

  function renderGuide() {
    const container = document.querySelector('[data-guide-content]');
    if (!container) return;
    const guide = data.guide[language];
    // All guide markup is authored locally and shipped in the static bundle.
    container.innerHTML = guide.map(part => `<section id="${part.id}"><h2>${part.title}</h2>${part.html}</section>`).join('') +
      `<p><a href="https://github.com/MrMybal/CyUEDIFF/blob/main/docs/ROADMAP.md" target="_blank" rel="noopener noreferrer">${text('guide.reference')} ↗</a></p>`;
    const nav = document.querySelector('[data-guide-nav]');
    nav.setAttribute('aria-label', text('guide.chapters'));
    nav.innerHTML = guide.map(part => `<a href="#${part.id}">${part.title}</a>`).join('');
  }

  function updateLightbox() {
    if (!selectedShot || !dialog.open) return;
    const img = selectedShot.querySelector('img');
    enlarged.src = img.src;
    enlarged.alt = img.alt;
    caption.textContent = text(selectedShot.dataset.caption);
  }

  function setLanguage(next, updateURL = true) {
    language = languages.includes(next) ? next : 'en';
    renderGuide();
    document.documentElement.lang = language;
    document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = text(el.dataset.i18n); });
    document.querySelectorAll('[data-label]').forEach(el => el.setAttribute('aria-label', text(el.dataset.label)));
    document.querySelectorAll('[data-language]').forEach(el => el.setAttribute('aria-pressed', String(el.dataset.language === language)));
    document.querySelectorAll('[data-guide]').forEach(el => { el.href = `guide.html?lang=${language}#${el.dataset.guide}`; });
    document.querySelectorAll('[data-home], .brand').forEach(el => { el.href = `index.html?lang=${language}`; });
    document.querySelectorAll('[data-feature-link]').forEach(el => { el.href = `index.html?lang=${language}#review`; });
    document.querySelectorAll('[data-main-download]').forEach(el => { el.href = `index.html?lang=${language}#download`; });
    document.querySelectorAll('[data-shot]').forEach(img => {
      const src = `assets/screenshots/${img.dataset.shot}-${language}.png`;
      if (img.getAttribute('src') !== src) img.src = src;
      const button = img.closest('[data-zoom]');
      const key = button ? button.dataset.caption : data.shots[img.dataset.shot];
      img.alt = text(key);
      if (button) button.setAttribute('aria-label', `${text('zoom.open')} — ${text(key)}`);
    });
    document.querySelector('meta[name="description"]')?.setAttribute('content', text('meta.description'));
    document.querySelector('meta[property="og:description"]')?.setAttribute('content', text('meta.description'));
    document.title = document.body.classList.contains('guide-page') ? `${text('guide.title')} — CyUEDiff` : text('meta.title');
    try { localStorage.setItem('cyuediff.site.language', language); } catch (_) {}
    if (updateURL) {
      const url = new URL(location.href);
      url.searchParams.set('lang', language);
      history.replaceState(null, '', url);
    }
    updateLightbox();
  }
  document.querySelectorAll('[data-language]').forEach(button => button.addEventListener('click', () => setLanguage(button.dataset.language)));
  window.addEventListener('popstate', () => {
    const next = new URL(location.href).searchParams.get('lang');
    setLanguage(languages.includes(next) ? next : 'en', false);
  });

  const menu = document.querySelector('.menu-toggle');
  const nav = document.getElementById('main-nav');
  function closeMenu() { menu?.setAttribute('aria-expanded', 'false'); nav?.classList.remove('is-open'); }
  menu?.addEventListener('click', () => {
    const open = menu.getAttribute('aria-expanded') !== 'true';
    menu.setAttribute('aria-expanded', String(open)); nav.classList.toggle('is-open', open);
  });
  nav?.addEventListener('click', event => { if (event.target.closest('a')) closeMenu(); });
  window.addEventListener('keydown', event => { if (event.key === 'Escape' && menu?.getAttribute('aria-expanded') === 'true') { closeMenu(); menu.focus(); } });
  window.matchMedia('(min-width: 761px)').addEventListener('change', event => { if (event.matches) closeMenu(); });

  const tabs = Array.from(document.querySelectorAll('[role="tab"]'));
  function activateTab(tab, focus = false) {
    tabs.forEach(item => {
      const active = item === tab;
      item.setAttribute('aria-selected', String(active)); item.tabIndex = active ? 0 : -1;
      document.getElementById(item.getAttribute('aria-controls')).hidden = !active;
    });
    if (focus) tab.focus();
  }
  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => activateTab(tab));
    tab.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
      if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
      if (event.key === 'Home') next = 0;
      if (event.key === 'End') next = tabs.length - 1;
      if (next !== undefined) { event.preventDefault(); activateTab(tabs[next], true); }
    });
  });

  function openShot(button) {
    selectedShot = button;
    gallery = Array.from(document.querySelectorAll('[data-zoom]')).filter(el => !el.closest('[hidden]'));
    if (!dialog.open) { dialog.showModal(); document.body.classList.add('lightbox-open'); }
    updateLightbox();
  }
  document.querySelectorAll('[data-zoom]').forEach(button => button.addEventListener('click', () => openShot(button)));
  function moveShot(direction) {
    if (!gallery.length) return;
    selectedShot = gallery[(gallery.indexOf(selectedShot) + direction + gallery.length) % gallery.length];
    updateLightbox();
  }
  dialog?.querySelector('[data-close]').addEventListener('click', () => dialog.close());
  dialog?.querySelector('[data-previous]').addEventListener('click', () => moveShot(-1));
  dialog?.querySelector('[data-next]').addEventListener('click', () => moveShot(1));
  dialog?.addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft') { event.preventDefault(); moveShot(-1); }
    if (event.key === 'ArrowRight') { event.preventDefault(); moveShot(1); }
  });
  dialog?.addEventListener('click', event => {
    const rect = dialog.getBoundingClientRect();
    if (event.target === dialog && (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom)) dialog.close();
  });
  dialog?.addEventListener('close', () => {
    document.body.classList.remove('lightbox-open');
    selectedShot?.focus({preventScroll: true});
  });

  document.querySelectorAll('[data-copy]').forEach(button => button.addEventListener('click', async () => {
    const value = document.getElementById(button.dataset.copy).textContent;
    try {
      await navigator.clipboard.writeText(value);
      button.querySelector('[data-i18n]').textContent = text('core.copied');
      clearTimeout(copiedTimer);
      copiedTimer = setTimeout(() => { button.querySelector('[data-i18n]').textContent = text('core.copy'); }, 1800);
    } catch (_) {
      const range = document.createRange(); range.selectNodeContents(document.getElementById(button.dataset.copy));
      const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(range);
    }
  }));

  const download = data.download;
  document.querySelectorAll('[data-local-download]').forEach(el => { el.hidden = !download; });
  document.querySelectorAll('[data-no-download]').forEach(el => { el.hidden = !!download; });
  if (download) {
    document.querySelectorAll('[data-download-link]').forEach(link => { link.href = download.href; link.setAttribute('download', download.filename); });
    document.querySelectorAll('[data-checksum]').forEach(el => { el.textContent = download.sha256; });
  }
  const sections = Array.from(document.querySelectorAll('[data-chapter]'));
  const chapters = Array.from(document.querySelectorAll('.chapters a'));
  let scheduled = false;
  function updateChapters() {
    let current = sections[0]?.id;
    sections.forEach(section => { if (section.getBoundingClientRect().top <= 170) current = section.id; });
    chapters.forEach(link => {
      if (link.hash === '#' + current) link.setAttribute('aria-current', 'location');
      else link.removeAttribute('aria-current');
    }); scheduled = false;
  }
  window.addEventListener('scroll', () => { if (!scheduled) { scheduled = true; requestAnimationFrame(updateChapters); } }, {passive: true});
  setLanguage(language, false);
  updateChapters();
})();
