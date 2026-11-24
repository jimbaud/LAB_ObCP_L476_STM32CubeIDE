/* Courbes sur une abscisse d'indice d'échantillon, comme dans ObCP Control Android. */
(function (root) {
  "use strict";
  const clamp = (n, lo, hi) => Math.max(lo, Math.min(n, hi));
  const axisColors = {acc: ["--axis-x", "--axis-y", "--axis-z"], temp: ["--axis-t"]};
  const formatTick = x => x.toLocaleString("fr-FR", {maximumFractionDigits: 3});
  const LIMIT = 3000;

  class Charts {
    constructor(acc, temp, onView) {
      this.canvases = {acc, temp};
      this.onView = onView;
      this.span = 60;
      this.pending = false;
      this.clear();
      for (const canvas of [acc, temp]) this.addGestures(canvas);
      const scheme = root.matchMedia("(prefers-color-scheme: dark)");
      if (scheme.addEventListener) scheme.addEventListener("change", () => this.refresh());
      else if (scheme.addListener) scheme.addListener(() => this.refresh());
      if ("ResizeObserver" in root) {
        this.resize = new ResizeObserver(() => this.refresh());
        [acc,temp].forEach(el => this.resize.observe(el));
      } else root.addEventListener("resize", () => this.refresh());
    }
    clear() {
      this.rows = {acc: [], temp: []};
      this.count = {acc: 0, temp: 0};
      this.ends = null; // En suivi direct, chaque capteur garde son propre indice.
      this.refresh();
    }
    add(kind, values) {
      const rows = this.rows[kind];
      rows.push({i: this.count[kind]++, values});
      if (rows.length > LIMIT) rows.shift();
      this.refresh();
    }
    follow() { this.ends = null; this.refresh(); }
    zoom(span) { this.span = clamp(Math.round(span), 10, 600); this.refresh(); }
    latest(kind) { return Math.max(0, this.count[kind] - 1); }
    getEnd(kind) {
      const rows = this.rows[kind];
      if (!rows.length) return this.span - 1;
      const earliestEnd = Math.min(this.latest(kind), rows[0].i + this.span - 1);
      return this.ends ? clamp(this.ends[kind], earliestEnd, this.latest(kind)) : this.latest(kind);
    }
    pan(step) {
      if (!step) return;
      const ends = {};
      for (const kind of ["acc","temp"]) {
        const low = this.rows[kind][0]?.i || 0;
        const last = this.latest(kind);
        ends[kind] = clamp(this.getEnd(kind) + step, Math.min(last, low + this.span - 1), last);
      }
      this.ends = ["acc","temp"].every(k => ends[k] >= this.latest(k)) ? null : ends;
      this.refresh();
    }
    refresh() {
      if (this.pending) return;
      this.pending = true;
      requestAnimationFrame(() => {
        this.pending = false;
        for (const [kind, canvas] of Object.entries(this.canvases)) this.draw(kind, canvas);
        const past = ["acc","temp"].some(k => this.rows[k].length && this.getEnd(k) - this.span + 1 > this.rows[k][0].i);
        this.onView(this.span, this.ends === null, past);
      });
    }
    draw(kind, canvas) {
      const rect = canvas.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      const w = rect.width, h = rect.height, px = Math.min(3,window.devicePixelRatio || 1);
      if (canvas.width !== Math.round(w*px)) canvas.width = Math.round(w*px);
      if (canvas.height !== Math.round(h*px)) canvas.height = Math.round(h*px);
      const ctx = canvas.getContext("2d");
      const style = getComputedStyle(canvas);
      const colorOf = name => style.getPropertyValue(name).trim();
      const l = 38, r = w-12, top = 9, bottom = h-24;
      ctx.setTransform(px,0,0,px,0,0);
      ctx.clearRect(0,0,w,h);
      const all = this.rows[kind];
      const start = Math.max(all[0]?.i || 0, this.getEnd(kind) - this.span + 1);
      const end = start + this.span - 1;
      const data = all.slice(Math.max(0,start - (all[0]?.i || 0)), end - (all[0]?.i || 0) + 1);
      let lo = kind === "acc" ? -1 : 20, hi = kind === "acc" ? 1 : 30;
      if (data.length) {
        const values = data.flatMap(row => row.values);
        const vMin = Math.min(...values), vMax = Math.max(...values);
        if (kind === "acc") {
          const extent = Math.max(1, Math.ceil(Math.max(Math.abs(vMin),Math.abs(vMax))*2)/2);
          lo = -extent; hi = extent;
        } else {
          const step = Math.max(0.5, 10**Math.floor(Math.log10(Math.max(vMax-vMin,1))));
          lo = Math.floor((vMin-.08*step)/step)*step;
          hi = Math.ceil((vMax+.08*step)/step)*step;
        }
      }
      const x = i => l+(i-start)/(end-start)*(r-l);
      const y = v => bottom-(v-lo)/(hi-lo)*(bottom-top);
      ctx.font = '11px "IBM Plex Mono",Consolas,monospace';
      ctx.fillStyle = colorOf("--chart-text");ctx.strokeStyle = colorOf("--grid");ctx.lineWidth = 1;
      for (let tick=0; tick<=4; tick++) {
        const v=lo+(hi-lo)*tick/4, yy=y(v);
        ctx.beginPath();ctx.moveTo(l,yy);ctx.lineTo(r,yy);ctx.stroke();
        ctx.textAlign="right";ctx.fillText(formatTick(v),l-6,yy+3);
      }
      const divisions = w < 350 ? 3 : 5;
      for (let tick=0; tick<=divisions; tick++) {
        const i=start+Math.round(tick*(end-start)/divisions), xx=x(i);
        ctx.beginPath();ctx.setLineDash([2,4]);ctx.moveTo(xx,top);ctx.lineTo(xx,bottom);ctx.stroke();ctx.setLineDash([]);
        ctx.textAlign=tick===0?"left":tick===divisions?"right":"center";ctx.fillText(String(i),xx,bottom+16);
      }
      if (!data.length) {
        ctx.textAlign="center";ctx.fillStyle=colorOf("--surface");ctx.fillRect((l+r)/2-82,(top+bottom)/2-13,164,25);
        ctx.fillStyle=colorOf("--chart-text");ctx.font="13px system-ui,sans-serif";ctx.fillText("En attente de mesures",(l+r)/2,(top+bottom)/2+4);
        return;
      }
      const trace = series => {
        ctx.beginPath();
        data.forEach((row,i) => i === 0 ? ctx.moveTo(x(row.i),y(row.values[series])) : ctx.lineTo(x(row.i),y(row.values[series])));
      };
      ctx.save();ctx.beginPath();ctx.rect(l-3,top-3,r-l+6,bottom-top+6);ctx.clip();
      axisColors[kind].map(colorOf).forEach((color,series) => {
        trace(series);
        if (kind === "temp") {
          const grad=ctx.createLinearGradient(0,top,0,bottom);grad.addColorStop(0,"#df9e4738");grad.addColorStop(1,"#df9e4705");
          ctx.lineTo(x(data.at(-1).i),bottom);ctx.lineTo(x(data[0].i),bottom);ctx.closePath();ctx.fillStyle=grad;ctx.fill();trace(series);
        }
        ctx.strokeStyle=color;ctx.lineWidth=2.1;ctx.lineJoin="round";ctx.stroke();
        const last=data.at(-1);
        ctx.beginPath();ctx.arc(x(last.i),y(last.values[series]),2.9,0,2*Math.PI);ctx.fillStyle=color;ctx.fill();
      });
      ctx.restore();
    }
    addGestures(canvas) {
      const pointers=new Map();
      let origin=0, moved=0, baseSpan=60, distance=0;
      const separation=() => {const pts=[...pointers.values()];return Math.hypot(pts[0].x-pts[1].x,pts[0].y-pts[1].y);};
      canvas.addEventListener("pointerdown",e=>{
        if (e.button!==0) return;
        canvas.setPointerCapture(e.pointerId);
        pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
        if(pointers.size===1){origin=e.clientX;moved=0;}
        if(pointers.size===2){distance=separation();baseSpan=this.span;}
      });
      canvas.addEventListener("pointermove",e=>{
        if(!pointers.has(e.pointerId)) return;
        pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});
        if(pointers.size===2 && distance>0) {this.zoom(baseSpan*distance/Math.max(1,separation()));return;}
        if(pointers.size!==1) return;
        const samples=Math.round((e.clientX-origin)*this.span/Math.max(1,canvas.clientWidth-50));
        this.pan(moved-samples);moved=samples;
      });
      const remove=e=>{
        pointers.delete(e.pointerId);
        if(pointers.size===1){origin=[...pointers.values()][0].x;moved=0;}
      };
      canvas.addEventListener("pointerup",remove);canvas.addEventListener("pointercancel",remove);
      canvas.addEventListener("wheel",e=>{
        if (!e.deltaY) return;
        e.preventDefault();this.zoom(this.span*(e.deltaY<0 ? 0.80 : 1.25));
      },{passive:false});
    }
  }
  root.ObCPCharts = Charts;
})(globalThis);
