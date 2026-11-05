// outils.js - flèches entre deux éléments d'une figure, tracées une fois la
// mise en page terminée (positions lues par getBoundingClientRect).
//
//   relier("#a", "#b", { de: "droite", vers: "gauche", couleur: "var(--bleu)",
//                        forme: "droit" | "coude" | "courbe", tirets: false,
//                        texte: "étiquette", double: false, epaisseur: 2,
//                        decalage_de: [dx, dy], decalage_vers: [dx, dy],
//                        position_texte: 0.5, texte_decalage: [dx, dy] })
//
// La figure (élément .figure) reçoit un calque SVG « svg.fleches ».

const SVG = "http://www.w3.org/2000/svg";
let numero_marqueur = 0;

function calque(figure) {
  let svg = figure.querySelector(":scope > svg.fleches");
  if (!svg) {
    svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("class", "fleches");
    svg.appendChild(document.createElementNS(SVG, "defs"));
    figure.appendChild(svg);
  }
  return svg;
}

function ancre(element, cote, figure, decalage) {
  const r = element.getBoundingClientRect();
  const f = figure.getBoundingClientRect();
  const x = r.left - f.left - figure.clientLeft;
  const y = r.top - f.top - figure.clientTop;
  const p = {
    droite: [x + r.width, y + r.height / 2],
    gauche: [x, y + r.height / 2],
    haut: [x + r.width / 2, y],
    bas: [x + r.width / 2, y + r.height],
    centre: [x + r.width / 2, y + r.height / 2],
  }[cote];
  return [p[0] + (decalage ? decalage[0] : 0), p[1] + (decalage ? decalage[1] : 0)];
}

const DIRECTION = { droite: [1, 0], gauche: [-1, 0], haut: [0, -1], bas: [0, 1], centre: [0, 0] };

function marqueur(svg, couleur, taille) {
  const id = "m" + (++numero_marqueur);
  const m = document.createElementNS(SVG, "marker");
  m.setAttribute("id", id);
  m.setAttribute("viewBox", "0 0 10 10");
  m.setAttribute("refX", "8.5");
  m.setAttribute("refY", "5");
  m.setAttribute("markerUnits", "userSpaceOnUse");
  m.setAttribute("markerWidth", taille);
  m.setAttribute("markerHeight", taille);
  m.setAttribute("orient", "auto-start-reverse");
  const p = document.createElementNS(SVG, "path");
  p.setAttribute("d", "M 0 0.8 L 9.5 5 L 0 9.2 L 2.6 5 Z");
  p.setAttribute("style", "fill:" + couleur);
  m.appendChild(p);
  svg.querySelector("defs").appendChild(m);
  return id;
}

// Ligne brisée à coins arrondis
function chemin_arrondi(points, rayon) {
  let d = "M " + points[0][0] + " " + points[0][1];
  for (let i = 1; i < points.length - 1; i++) {
    const [ax, ay] = points[i - 1], [bx, by] = points[i], [cx, cy] = points[i + 1];
    const l1 = Math.hypot(bx - ax, by - ay), l2 = Math.hypot(cx - bx, cy - by);
    const r = Math.min(rayon, l1 / 2, l2 / 2);
    const p1 = [bx - (bx - ax) / l1 * r, by - (by - ay) / l1 * r];
    const p2 = [bx + (cx - bx) / l2 * r, by + (cy - by) / l2 * r];
    d += " L " + p1[0] + " " + p1[1] + " Q " + bx + " " + by + " " + p2[0] + " " + p2[1];
  }
  const fin = points[points.length - 1];
  return d + " L " + fin[0] + " " + fin[1];
}

function relier(de, vers, o = {}) {
  const a = typeof de === "string" ? document.querySelector(de) : de;
  const b = typeof vers === "string" ? document.querySelector(vers) : vers;
  const figure = a.closest(".figure");
  const svg = calque(figure);
  const cote_a = o.de || "droite", cote_b = o.vers || "gauche";
  const p1 = ancre(a, cote_a, figure, o.decalage_de);
  const p2 = ancre(b, cote_b, figure, o.decalage_vers);
  const couleur = o.couleur || "var(--discret)";
  const epaisseur = o.epaisseur || 2;

  let d, milieu;
  const forme = o.forme || "droit";
  if (forme === "droit") {
    d = "M " + p1[0] + " " + p1[1] + " L " + p2[0] + " " + p2[1];
    milieu = [(p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2];
  } else if (forme === "coude") {
    const horizontal = cote_a === "droite" || cote_a === "gauche";
    const t = o.position_coude ?? 0.5;
    let pts;
    if (horizontal) {
      const mx = p1[0] + (p2[0] - p1[0]) * t;
      pts = [p1, [mx, p1[1]], [mx, p2[1]], p2];
    } else {
      const my = p1[1] + (p2[1] - p1[1]) * t;
      pts = [p1, [p1[0], my], [p2[0], my], p2];
    }
    d = chemin_arrondi(pts, 10);
    milieu = [(pts[1][0] + pts[2][0]) / 2, (pts[1][1] + pts[2][1]) / 2];
  } else {                                   // courbe de Bézier
    const da = DIRECTION[cote_a], db = DIRECTION[cote_b];
    const k = o.tension || Math.max(40, Math.hypot(p2[0] - p1[0], p2[1] - p1[1]) * 0.4);
    const c1 = [p1[0] + da[0] * k, p1[1] + da[1] * k];
    const c2 = [p2[0] + db[0] * k, p2[1] + db[1] * k];
    d = "M " + p1 + " C " + c1 + " " + c2 + " " + p2;
    milieu = [0.125 * p1[0] + 0.375 * c1[0] + 0.375 * c2[0] + 0.125 * p2[0],
              0.125 * p1[1] + 0.375 * c1[1] + 0.375 * c2[1] + 0.125 * p2[1]];
  }

  const chemin = document.createElementNS(SVG, "path");
  chemin.setAttribute("d", d);
  chemin.setAttribute("style", "fill:none;stroke:" + couleur + ";stroke-width:" + epaisseur +
                      ";stroke-linecap:round;stroke-linejoin:round" +
                      (o.tirets ? ";stroke-dasharray:" + (o.tirets === true ? "6 6" : o.tirets) : ""));
  if (o.fleche !== false) {
    const id = marqueur(svg, couleur, o.taille_fleche || 11);
    chemin.setAttribute("marker-end", "url(#" + id + ")");
    if (o.double) chemin.setAttribute("marker-start", "url(#" + id + ")");
  }
  svg.appendChild(chemin);

  if (o.texte) {
    if (o.position_texte !== undefined && forme === "droit") {
      milieu = [p1[0] + (p2[0] - p1[0]) * o.position_texte, p1[1] + (p2[1] - p1[1]) * o.position_texte];
    }
    const dx = o.texte_decalage ? o.texte_decalage[0] : 0;
    const dy = o.texte_decalage ? o.texte_decalage[1] : 0;
    const groupe = document.createElementNS(SVG, "g");
    const fond = document.createElementNS(SVG, "rect");
    fond.setAttribute("class", "etiquette-fond");
    groupe.appendChild(fond);
    const lignes = String(o.texte).split("\n");
    const texte = document.createElementNS(SVG, "text");
    texte.setAttribute("text-anchor", o.ancre_texte || "middle");
    if (o.style_texte) texte.setAttribute("style", o.style_texte);
    lignes.forEach((ligne, i) => {
      const t = document.createElementNS(SVG, "tspan");
      t.setAttribute("x", milieu[0] + dx);
      t.setAttribute("y", milieu[1] + dy + (i - (lignes.length - 1) / 2) * 16 + 4.5);
      t.textContent = ligne;
      texte.appendChild(t);
    });
    groupe.appendChild(texte);
    svg.appendChild(groupe);
    if (o.fond_texte !== false) {
      const bb = texte.getBBox();
      fond.setAttribute("x", bb.x - 6);
      fond.setAttribute("y", bb.y - 2);
      fond.setAttribute("width", bb.width + 12);
      fond.setAttribute("height", bb.height + 4);
      fond.setAttribute("rx", 6);
    }
  }
  return chemin;
}

// Le générateur attend ce signal avant la capture.
window.figure_prete = false;
function quand_pret(fonction) {
  document.fonts.ready.then(() => {
    const images = Array.from(document.images).map(i => i.complete ? null : new Promise(r => { i.onload = i.onerror = r; }));
    Promise.all(images).then(() => {
      requestAnimationFrame(() => { fonction(); window.figure_prete = true; });
    });
  });
}
