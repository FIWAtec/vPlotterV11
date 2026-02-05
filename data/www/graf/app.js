(() => {
  const el = (id) => document.getElementById(id);
  const c = el('c');
  const ctx = c.getContext('2d');

  const hudZoom = el('hudZoom');
  const hudMouse = el('hudMouse');
  const areasList = el('areasList');
  const holesList = el('holesList');

  const state = {
    wallW: 6000,
    wallH: 1400,
    topD: 2000,
    nAreas: 4,
    safeXFrac: 0.2,
    safeYFrac: 0.2,
    holeFromFloor: 2000,
    startX: 0,

    scale: 0.12,
    offX: 60,
    offY: 60,
    dragging: false,
    dragStart: {x:0,y:0, ox:0, oy:0},

    img: null,
    imgBaseName: 'bild',
    imgAlpha: 0.55,
    imgX: 0, imgY: 0, imgW: 6000, imgH: 1400,
    lockAspect: true,
    imgAspect: 1,

    // image scale step
    scaleStepPct: 5,
    // image move step (mm)
    moveStepMm: 10,

    safeX: 0,
    safeY: 0,
    paintW: 0,
    paintH: 0,
    paintY: 0,
    areas: [],
    holes: []
  };

  function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }

  let viewportFitTimer = 0;
  function scheduleViewportFit(delayMs = 80) {
    clearTimeout(viewportFitTimer);
    viewportFitTimer = window.setTimeout(() => {
      resize();
      centerView();
      draw();
    }, Math.max(0, delayMs));
  }

  function resize(){
    const dpr = Math.max(1, Math.min(2, window.devicePixelRatio || 1));
    c.width = Math.floor(c.clientWidth * dpr);
    c.height = Math.floor(c.clientHeight * dpr);
    ctx.setTransform(dpr,0,0,dpr,0,0);
    draw();
  }

  function readInputs(){
    state.wallW = Math.max(1, Number(el('wallW').value) || 1);
    state.wallH = Math.max(1, Number(el('wallH').value) || 1);
    state.topD  = Math.max(1, Number(el('topD').value) || 1);
    state.nAreas = Math.max(1, Math.floor(Number(el('nAreas').value) || 1));
    state.safeXFrac = clamp(Number(el('safeXFrac').value) || 0, 0, 0.49);
    state.safeYFrac = clamp(Number(el('safeYFrac').value) || 0, 0, 0.9);
    state.holeFromFloor = Math.max(0, Number(el('holeFromFloor').value) || 0);
    state.startX = Number(el('startX').value) || 0;

    state.imgAlpha = clamp(Number(el('imgAlpha').value) || 0.55, 0, 1);
    state.lockAspect = el('lockAspect').value === '1';

    state.imgX = Number(el('imgX').value) || 0;
    state.imgY = Number(el('imgY').value) || 0;
    state.imgW = Math.max(1, Number(el('imgW').value) || 1);
    state.imgH = Math.max(1, Number(el('imgH').value) || 1);
    state.scaleStepPct = Math.max(0.1, Math.min(50, Number(el('scaleStepPct').value) || 5));
    state.moveStepMm = Math.max(0.1, Math.min(1000, Number(el('moveStepMm').value) || 10));
  }

  function writeImageInputs(){
    el('imgAlpha').value = state.imgAlpha.toFixed(2);
    el('imgX').value = Math.round(state.imgX);
    el('imgY').value = Math.round(state.imgY);
    el('imgW').value = Math.round(state.imgW);
    el('imgH').value = Math.round(state.imgH);
    el('lockAspect').value = state.lockAspect ? '1':'0';
  }

  function compute(){
    state.safeX = state.topD * state.safeXFrac;
    state.safeY = state.topD * state.safeYFrac;

    state.paintW = Math.max(1, state.topD - 2*state.safeX);
    state.paintY = state.safeY;                 // global SAFE zone above
    state.paintH = Math.max(1, state.wallH - state.paintY);

    const holeYTop = clamp(state.wallH - state.holeFromFloor, 0, state.wallH);

    state.areas = [];
    state.holes = [];
    for(let i=0;i<state.nAreas;i++){
      const paintX = state.startX + i*state.paintW;
      const anchorL = paintX - state.safeX;
      const anchorR = anchorL + state.topD;

      const area = {
        i,
        paint: {x:paintX,y:state.paintY,w:state.paintW,h:state.paintH},
        anchorL:{x:anchorL,y:holeYTop},
        anchorR:{x:anchorR,y:holeYTop}
      };
      state.areas.push(area);
      state.holes.push({tag:`B${i+1}L`, x:anchorL, yFloor:state.holeFromFloor, yTop:holeYTop});
      state.holes.push({tag:`B${i+1}R`, x:anchorR, yFloor:state.holeFromFloor, yTop:holeYTop});
    }
    renderLists();
  }

  function fmt(v){ return String(Math.round(v)).padStart(5,' '); }
  function mm(v){ return `${Math.round(v)} mm`; }

  function renderLists(){
    const header = `
      <div class="listCard">
        <div class="title">Übersicht</div>
        <div class="listGrid">
          <div class="metric"><span class="k">Wand</span><span class="v">${Math.round(state.wallW)} × ${Math.round(state.wallH)} mm</span></div>
          <div class="metric"><span class="k">TopDistance</span><span class="v">${mm(state.topD)}</span></div>
          <div class="metric"><span class="k">SafeX / SafeY</span><span class="v">${mm(state.safeX)} / ${mm(state.safeY)}</span></div>
          <div class="metric"><span class="k">Malfläche je Bereich</span><span class="v">${mm(state.paintW)} × ${mm(state.paintH)}</span></div>
        </div>
      </div>`;

    const areaCards = state.areas.map((a)=>{
      const x0 = a.paint.x;
      const x1 = a.paint.x + a.paint.w;
      return `
        <div class="listCard">
          <div class="title">Bereich ${a.i+1}</div>
          <div class="listGrid">
            <div class="metric"><span class="k">Malbereich X Start</span><span class="v">${mm(x0)}</span></div>
            <div class="metric"><span class="k">Malbereich X Ende</span><span class="v">${mm(x1)}</span></div>
            <div class="metric"><span class="k">Malbereich Y Start</span><span class="v">${mm(a.paint.y)}</span></div>
            <div class="metric"><span class="k">Malbereich Höhe</span><span class="v">${mm(a.paint.h)}</span></div>
          </div>
          <div class="holePair">
            <div class="holeLine">
              <div class="holeTag">B${a.i+1}L</div>
              <div class="holeValue">X: ${mm(a.anchorL.x)}</div>
              <div class="holeValue">Y vom Boden: ${mm(state.holeFromFloor)}</div>
              <div class="holeValue">Y von oben: ${mm(a.anchorL.y)}</div>
            </div>
            <div class="holeLine">
              <div class="holeTag">B${a.i+1}R</div>
              <div class="holeValue">X: ${mm(a.anchorR.x)}</div>
              <div class="holeValue">Y vom Boden: ${mm(state.holeFromFloor)}</div>
              <div class="holeValue">Y von oben: ${mm(a.anchorR.y)}</div>
            </div>
          </div>
        </div>`;
    }).join('');

    areasList.innerHTML = header + areaCards;

    const holesCards = state.holes.map((h)=>`
      <div class="listCard">
        <div class="title">${h.tag}</div>
        <div class="listGrid">
          <div class="metric"><span class="k">X</span><span class="v">${mm(h.x)}</span></div>
          <div class="metric"><span class="k">Y vom Boden</span><span class="v">${mm(h.yFloor)}</span></div>
          <div class="metric"><span class="k">Y von oben</span><span class="v">${mm(h.yTop)}</span></div>
        </div>
      </div>`).join('');

    holesList.innerHTML = `
      <div class="listCard">
        <div class="title">Bohrungsmaße (untereinander)</div>
        <div class="listGrid">
          <div class="metric"><span class="k">Wand</span><span class="v">${Math.round(state.wallW)} × ${Math.round(state.wallH)} mm</span></div>
          <div class="metric"><span class="k">Bohrhöhe vom Boden</span><span class="v">${mm(state.holeFromFloor)}</span></div>
          <div class="metric"><span class="k">Bohr-Y von oben</span><span class="v">${mm(clamp(state.wallH - state.holeFromFloor,0,state.wallH))}</span></div>
        </div>
      </div>
    ` + holesCards;
  }

  function worldToScreen(x,y){ return {x:x*state.scale+state.offX, y:y*state.scale+state.offY}; }
  function screenToWorld(x,y){ return {x:(x-state.offX)/state.scale, y:(y-state.offY)/state.scale}; }

  function draw(){
    const w=c.clientWidth, h=c.clientHeight;
    ctx.clearRect(0,0,w,h);
    ctx.fillStyle='#060b18'; ctx.fillRect(0,0,w,h);

    const p0 = worldToScreen(0,0);
    const p1 = worldToScreen(state.wallW,state.wallH);
    const wallX=p0.x, wallY=p0.y, wallW=p1.x-p0.x, wallH=p1.y-p0.y;

    // wall background
    ctx.fillStyle='rgba(62,91,140,0.78)';
    ctx.fillRect(wallX,wallY,wallW,wallH);

    // global safe top
    const safeH = worldToScreen(0,state.safeY).y - wallY;
    ctx.fillStyle='rgba(194,36,80,0.62)';
    ctx.fillRect(wallX,wallY,wallW,safeH);

    // image (between)
    if(state.img){
      const ix0 = worldToScreen(state.imgX,state.imgY);
      const ix1 = worldToScreen(state.imgX+state.imgW,state.imgY+state.imgH);
      ctx.save();
      ctx.globalAlpha = state.imgAlpha;
      ctx.drawImage(state.img, ix0.x, ix0.y, ix1.x-ix0.x, ix1.y-ix0.y);
      ctx.restore();
    }

    // paint areas
    for(const a of state.areas){
      const c0 = worldToScreen(a.paint.x,a.paint.y);
      const c1 = worldToScreen(a.paint.x+a.paint.w,a.paint.y+a.paint.h);
      const rx=c0.x, ry=c0.y, rw=c1.x-c0.x, rh=c1.y-c0.y;

      ctx.fillStyle='rgba(90,206,255,0.40)';
      ctx.fillRect(rx,ry,rw,rh);

      ctx.lineWidth=2;
      ctx.strokeStyle='rgba(89,165,255,0.95)';
      ctx.strokeRect(rx,ry,rw,rh);

      // label
      const compact = c.clientWidth < 900;
      ctx.font = compact ? '600 12px system-ui' : 'bold 14px system-ui';
      ctx.fillStyle='rgba(0,0,0,0.70)';
      ctx.fillText(`Bereich ${a.i+1}`, rx+8, ry+(compact ? 18 : 22));

      // belt V hints
      drawBeltV(a.anchorL.x,a.anchorL.y, a.paint.x+a.paint.w/2, a.paint.y+a.paint.h*0.35, '#5edbff');
      drawBeltV(a.anchorR.x,a.anchorR.y, a.paint.x+a.paint.w/2, a.paint.y+a.paint.h*0.35, '#7f8cff');
    }

    // holes + callouts
    for(const a of state.areas){
      drawHole(a.anchorL.x,a.anchorL.y, `B${a.i+1}L`);
      drawHole(a.anchorR.x,a.anchorR.y, `B${a.i+1}R`);
    }

    drawDims();
    hudZoom.textContent = state.scale.toFixed(3)+'×';
  }

  function drawHole(xw,yw,label){
    const p=worldToScreen(xw,yw);
    ctx.save();
    ctx.fillStyle='#ff2e2e';
    ctx.beginPath();
    ctx.arc(p.x,p.y,5,0,Math.PI*2);
    ctx.fill();

    // Keep canvas clean on phone: only short tag, full numbers are in list below
    ctx.font='600 11px system-ui';
    ctx.fillStyle='rgba(0,0,0,0.60)';
    const tw=ctx.measureText(label).width;
    ctx.fillRect(p.x+7,p.y-14,tw+10,14);
    ctx.fillStyle='#e9f1fb';
    ctx.fillText(label,p.x+12,p.y-3);
    ctx.restore();
  }

  function drawBeltV(ax,ay,cx,cy,color){
    const a=worldToScreen(ax,ay), b=worldToScreen(cx,cy);
    ctx.save();
    ctx.strokeStyle=color;
    ctx.lineWidth=2;
    ctx.beginPath(); ctx.moveTo(a.x,a.y); ctx.lineTo(b.x,b.y); ctx.stroke();
    ctx.restore();
  }

  function drawDims(){
    const p0=worldToScreen(0,0), p1=worldToScreen(state.wallW,0);
    const compact = c.clientWidth < 900;

    ctx.save();
    ctx.strokeStyle='rgba(0,0,0,0.35)';
    ctx.lineWidth=2;
    ctx.beginPath();
    ctx.moveTo(p0.x,p0.y-18);
    ctx.lineTo(p1.x,p1.y-18);
    ctx.stroke();

    const t=`Gesamtbreite: ${Math.round(state.wallW)} mm`;
    ctx.font = compact ? '600 12px system-ui' : 'bold 14px system-ui';
    const tw=ctx.measureText(t).width;
    ctx.fillStyle='rgba(0,0,0,0.60)';
    ctx.fillRect((p0.x+p1.x)/2 - tw/2 - 8, p0.y - (compact ? 38 : 44), tw+16, compact ? 20 : 22);
    ctx.fillStyle='#e9f1fb';
    ctx.fillText(t,(p0.x+p1.x)/2 - tw/2, p0.y - (compact ? 24 : 28));

    if(!compact){
      const q0=worldToScreen(0,0), q1=worldToScreen(0,state.wallH);
      ctx.beginPath();
      ctx.moveTo(q0.x-20,q0.y);
      ctx.lineTo(q1.x-20,q1.y);
      ctx.stroke();
      const th=`Gesamthöhe: ${Math.round(state.wallH)} mm`;
      ctx.translate(q0.x-44,(q0.y+q1.y)/2);
      ctx.rotate(-Math.PI/2);
      const thw=ctx.measureText(th).width;
      ctx.fillStyle='rgba(0,0,0,0.60)';
      ctx.fillRect(-thw/2-8,-14,thw+16,22);
      ctx.fillStyle='#e9f1fb';
      ctx.fillText(th,-thw/2,2);
    }

    ctx.fillStyle='rgba(0,0,0,0.55)';
    ctx.fillRect(p0.x+10,p0.y+10,205,22);
    ctx.fillStyle='#e9f1fb';
    ctx.font='12px system-ui';
    ctx.fillText(`SAFE oben: ${Math.round(state.safeY)} mm`, p0.x+18, p0.y+26);
    ctx.restore();
  }

  async function loadFile(file){
    if(!file) return;
    const name=file.name.toLowerCase();
    if(name.endsWith('.svg')){
      const txt=await file.text();
      const url=URL.createObjectURL(new Blob([txt],{type:'image/svg+xml'}));
      await loadImageUrl(url);
      URL.revokeObjectURL(url);
      return;
    }
    const url=URL.createObjectURL(file);
    await loadImageUrl(url);
    URL.revokeObjectURL(url);
  }

  function loadImageUrl(url){
    return new Promise((resolve,reject)=>{
      const img=new Image();
      img.onload=()=>{ state.img=img; state.imgAspect=img.width/img.height; writeImageInputs(); draw(); resolve(); };
      img.onerror=reject;
      img.src=url;
    });
  }

  function fitImageToWall(){
    state.imgX=0; state.imgY=0; state.imgW=state.wallW; state.imgH=state.wallH;
    writeImageInputs(); draw();
  }

  function fitImageToPaintUnion(){
    const x0 = state.startX;
    const x1 = state.startX + state.nAreas * state.paintW;
    const y0 = state.paintY;
    const y1 = state.paintY + state.paintH;
    state.imgX=x0; state.imgY=y0; state.imgW=(x1-x0); state.imgH=(y1-y0);
    writeImageInputs(); draw();
  }

  // Fit only image height to the paint union height (keeps X/W, adjusts Y/H)
  function fitImageToPaintHeight() {
    const y0 = state.paintY;
    const y1 = state.paintY + state.paintH;
    state.imgY = y0;
    state.imgH = (y1 - y0);
    if (state.lockAspect && state.img) state.imgW = state.imgH * state.imgAspect;
    writeImageInputs();
    draw();
  }

  // Fit only image width to the paint union width (keeps Y/H, adjusts X/W)
  function fitImageToPaintWidth() {
    const x0 = state.startX;
    const x1 = state.startX + state.nAreas * state.paintW;
    state.imgX = x0;
    state.imgW = (x1 - x0);
    if (state.lockAspect && state.img) state.imgH = state.imgW / state.imgAspect;
    writeImageInputs();
    draw();
  }

  function getTopOcclusionPx(){
    const panel = document.getElementById('panel');
    if (!panel || panel.classList.contains('collapsed')) return 0;
    const r = panel.getBoundingClientRect();
    return Math.max(0, Math.min(window.innerHeight, r.bottom));
  }

  function centerView(){
    const vw = c.clientWidth;
    const vh = c.clientHeight;
    const topOcc = getTopOcclusionPx();
    const pad = 16;

    const availW = Math.max(40, vw - pad * 2);
    const availH = Math.max(40, vh - topOcc - pad * 2);

    const sx = availW / state.wallW;
    const sy = availH / state.wallH;
    state.scale = Math.max(0.01, Math.min(sx, sy));

    const usedW = state.wallW * state.scale;
    const usedH = state.wallH * state.scale;

    state.offX = pad + (availW - usedW) / 2;
    state.offY = topOcc + pad + (availH - usedH) / 2;
    draw();
  }

  function exportViewPng(){
    const a=document.createElement('a');
    a.download = `${state.imgBaseName}__bohrplan_ansicht.png`;
    a.href=c.toDataURL('image/png');
    a.click();
  }

  
  
  function moveImage(dx, dy) {
    // Move image in world-mm coordinates
    state.imgX += dx;
    state.imgY += dy;
    writeImageInputs();
    draw();
  }

function scaleImageAxis(axis, dir) {
    // axis: 'x' or 'y', dir: +1 or -1
    const step = (state.scaleStepPct || 5) / 100.0;
    const factor = dir > 0 ? (1 + step) : (1 / (1 + step));

    // Scale around image center in world space
    const cx = state.imgX + state.imgW / 2;
    const cy = state.imgY + state.imgH / 2;

    if (axis === 'x') {
      const newW = Math.max(1, state.imgW * factor);
      state.imgW = newW;
      state.imgX = cx - newW / 2;
    } else {
      const newH = Math.max(1, state.imgH * factor);
      state.imgH = newH;
      state.imgY = cy - newH / 2;
    }

    // IMPORTANT: axis stretch requested -> no aspect enforcement here
    writeImageInputs();
    draw();
  }

function exportSplit(){
    const pxPerMM = 1.0;
    const outW = Math.round(state.paintW*pxPerMM);
    const outH = Math.round(state.paintH*pxPerMM);

    for(const a of state.areas){
      const oc=document.createElement('canvas');
      oc.width=outW; oc.height=outH;
      const ox=oc.getContext('2d');
      ox.clearRect(0,0,outW,outH);

      if(state.img){
        const pr=a.paint;
        const ix0 = (pr.x - state.imgX)/state.imgW * state.img.width;
        const iy0 = (pr.y - state.imgY)/state.imgH * state.img.height;
        const ix1 = (pr.x+pr.w - state.imgX)/state.imgW * state.img.width;
        const iy1 = (pr.y+pr.h - state.imgY)/state.imgH * state.img.height;

        const sx=clamp(ix0,0,state.img.width);
        const sy=clamp(iy0,0,state.img.height);
        const sw=clamp(ix1,0,state.img.width)-sx;
        const sh=clamp(iy1,0,state.img.height)-sy;

        const dx=Math.round((sx-ix0)/(ix1-ix0)*outW);
        const dy=Math.round((sy-iy0)/(iy1-iy0)*outH);
        const dw=Math.round(sw/(ix1-ix0)*outW);
        const dh=Math.round(sh/(iy1-iy0)*outH);

        if(sw>1 && sh>1) ox.drawImage(state.img, sx,sy,sw,sh, dx,dy,dw,dh);
      }

      // border + label
      ox.strokeStyle='rgba(30,80,255,0.95)';
      ox.lineWidth=4;
      ox.strokeRect(0,0,outW,outH);
      ox.fillStyle='rgba(0,0,0,0.6)';
      ox.fillRect(10,10,220,34);
      ox.fillStyle='#fff';
      ox.font='bold 18px system-ui';
      ox.fillText(`Bereich ${a.i+1}`,20,34);

      const link=document.createElement('a');
      link.download = `${state.imgBaseName}__bereich_${String(a.i+1).padStart(2,'0')}.png`;
      link.href=oc.toDataURL('image/png');
      link.click();
    }
  }

  function onDown(e){
    state.dragging=true;
    state.dragStart.x=e.clientX; state.dragStart.y=e.clientY;
    state.dragStart.ox=state.offX; state.dragStart.oy=state.offY;
  }
  function onUp(){ state.dragging=false; }
  function onMove(e){
    const r=c.getBoundingClientRect();
    const mx=e.clientX-r.left, my=e.clientY-r.top;
    const w=screenToWorld(mx,my);
    hudMouse.textContent = `${Math.round(w.x)} , ${Math.round(w.y)} mm`;
    if(!state.dragging) return;
    state.offX = state.dragStart.ox + (e.clientX-state.dragStart.x);
    state.offY = state.dragStart.oy + (e.clientY-state.dragStart.y);
    draw();
  }
  function onWheel(e){
    // Ctrl/Cmd + wheel is browser/page zoom on desktop.
    // Do not hijack it. Let browser zoom, then auto-fit canvas.
    if (e.ctrlKey || e.metaKey) {
      scheduleViewportFit(40);
      scheduleViewportFit(180);
      return;
    }

    e.preventDefault();
    const r=c.getBoundingClientRect();
    const mx=e.clientX-r.left, my=e.clientY-r.top;
    const before=screenToWorld(mx,my);
    const zoom=Math.exp(-e.deltaY*0.0015);
    state.scale = clamp(state.scale*zoom,0.01,5);
    const after=screenToWorld(mx,my);
    state.offX += (after.x-before.x)*state.scale;
    state.offY += (after.y-before.y)*state.scale;
    draw();
  }

  function applyImageWHFromInputs(){
    const oldW=state.imgW, oldH=state.imgH;
    readInputs();
    if(!state.lockAspect || !state.img){ draw(); return; }
    const wChanged = Math.abs(state.imgW-oldW)>0.5;
    const hChanged = Math.abs(state.imgH-oldH)>0.5;
    if(wChanged && !hChanged) state.imgH = state.imgW/state.imgAspect;
    if(hChanged && !wChanged) state.imgW = state.imgH*state.imgAspect;
    writeImageInputs();
    draw();
  }


  // Touch support: one finger pan, two finger pinch zoom
  let touchMode = null;
  let touchStart = null;

  function touchDist(a,b){
    const dx = a.clientX - b.clientX;
    const dy = a.clientY - b.clientY;
    return Math.hypot(dx,dy);
  }
  function touchMid(a,b){
    return {x:(a.clientX+b.clientX)/2, y:(a.clientY+b.clientY)/2};
  }
  function onTouchStart(e){
    if(!e.touches || e.touches.length===0) return;
    if(e.touches.length===1){
      const t=e.touches[0];
      touchMode='pan';
      touchStart={x:t.clientX,y:t.clientY,ox:state.offX,oy:state.offY};
    } else if(e.touches.length>=2){
      const a=e.touches[0], b=e.touches[1];
      const m=touchMid(a,b);
      const r=c.getBoundingClientRect();
      const mx=m.x-r.left, my=m.y-r.top;
      touchMode='pinch';
      touchStart={dist:touchDist(a,b), scale:state.scale, mx, my};
    }
  }
  function onTouchMove(e){
    if(!e.touches || e.touches.length===0) return;
    if(touchMode==='pan' && e.touches.length===1){
      e.preventDefault();
      const t=e.touches[0];
      state.offX = touchStart.ox + (t.clientX - touchStart.x);
      state.offY = touchStart.oy + (t.clientY - touchStart.y);
      draw();
    } else if(e.touches.length>=2){
      e.preventDefault();
      const a=e.touches[0], b=e.touches[1];
      const m=touchMid(a,b);
      const r=c.getBoundingClientRect();
      const mx=m.x-r.left, my=m.y-r.top;
      const before=screenToWorld(mx,my);
      const ratio = touchDist(a,b) / Math.max(1,touchStart.dist);
      state.scale = clamp(touchStart.scale * ratio, 0.01, 5);
      const after=screenToWorld(mx,my);
      state.offX += (after.x-before.x)*state.scale;
      state.offY += (after.y-before.y)*state.scale;
      draw();
    }
  }
  function onTouchEnd(){
    if(!touchMode) return;
    touchMode=null;
    touchStart=null;
  }
  function wire(){
    el('btnGrafBack')?.addEventListener('click', () => {
      window.location.href = '/index.html';
    });
    el('apply').addEventListener('click', ()=>{ readInputs(); compute(); centerView(); draw(); });
    el('center').addEventListener('click', ()=>{ centerView(); });

    el('file').addEventListener('change', async (e)=>{
      const f=e.target.files && e.target.files[0];
      await loadFile(f);
      fitImageToPaintUnion();
    });

    el('fitWall').addEventListener('click', fitImageToWall);
    el('fitPaint').addEventListener('click', fitImageToPaintUnion);
    el('fitPaintY').addEventListener('click', fitImageToPaintHeight);
    el('fitPaintX').addEventListener('click', fitImageToPaintWidth);
    el('scaleXPlus').addEventListener('click', () => scaleImageAxis('x', +1));
    el('scaleXMinus').addEventListener('click', () => scaleImageAxis('x', -1));
    el('scaleYPlus').addEventListener('click', () => scaleImageAxis('y', +1));
    el('scaleYMinus').addEventListener('click', () => scaleImageAxis('y', -1));
    el('moveLeft').addEventListener('click', () => moveImage(-state.moveStepMm, 0));
    el('moveRight').addEventListener('click', () => moveImage(+state.moveStepMm, 0));
    el('moveUp').addEventListener('click', () => moveImage(0, -state.moveStepMm));
    el('moveDown').addEventListener('click', () => moveImage(0, +state.moveStepMm));

    el('exportPng').addEventListener('click', exportViewPng);
    el('exportSplit').addEventListener('click', exportSplit);

    const panel = document.getElementById('panel');
    const tp = el('togglePanel');
    const fab = el('openPanelFab');

    const updatePanelButtonLabel = () => {
      if (!panel) return;
      const collapsed = panel.classList.contains('collapsed');
      if (tp) tp.textContent = collapsed ? 'Einblenden' : 'Ausblenden';
      if (fab) fab.style.display = collapsed ? 'inline-flex' : 'none';
    };

    const openPanel = () => {
      if (!panel) return;
      panel.classList.remove('collapsed');
      updatePanelButtonLabel();
      centerView();
    };

    const closePanel = () => {
      if (!panel) return;
      panel.classList.add('collapsed');
      updatePanelButtonLabel();
      centerView();
    };

    if (panel && tp) {
      tp.addEventListener('click', () => {
        panel.classList.contains('collapsed') ? openPanel() : closePanel();
      });
    }
    if (fab) fab.addEventListener('click', openPanel);

    // Fold toggles for stacked windows
    const toggleFold = (btnId, contentId) => {
      const btn = el(btnId);
      const content = el(contentId);
      if (!btn || !content) return;
      btn.addEventListener('click', () => {
        const hidden = content.classList.toggle('hidden');
        btn.textContent = hidden ? 'Einblenden' : 'Ausblenden';
        requestAnimationFrame(centerView);
      });
    };
    toggleFold('toggleGeoInputs', 'geoContent');
    toggleFold('toggleImgInputs', 'imgContent');
    toggleFold('toggleLists', 'listsContent');

    // List toggles (clean readability)
    const toggleBox = (btnId, boxId) => {
      const btn = el(btnId);
      const box = el(boxId);
      if (!btn || !box) return;
      btn.addEventListener('click', () => {
        const hidden = box.style.display === 'none';
        box.style.display = hidden ? 'block' : 'none';
        btn.textContent = hidden ? 'Ausblenden' : 'Einblenden';
        requestAnimationFrame(centerView);
      });
    };
    toggleBox('toggleAreas', 'areasList');
    toggleBox('toggleHoles', 'holesList');

    // Quick mode: show only drilling measurements list
    const holesModeBtn = el('onlyHolesMode');
    if (holesModeBtn && panel) {
      holesModeBtn.addEventListener('click', () => {
        const onlyHoles = panel.classList.toggle('holes-only');
        holesModeBtn.textContent = onlyHoles ? 'Alles zeigen' : 'Nur Bohrungsmaße';
        requestAnimationFrame(centerView);
      });
    }

    ['wallW','wallH','topD','nAreas','safeXFrac','safeYFrac','holeFromFloor','startX']
      .forEach(id=>el(id).addEventListener('input', ()=>{ readInputs(); compute(); centerView(); draw(); }));

    ['imgAlpha','imgX','imgY','lockAspect','scaleStepPct','moveStepMm']
      .forEach(id=>el(id).addEventListener('input', ()=>{ readInputs(); compute(); draw(); }));

    ['imgW','imgH'].forEach(id=>el(id).addEventListener('input', applyImageWHFromInputs));

    c.addEventListener('mousedown', onDown);
    window.addEventListener('mouseup', onUp);
    window.addEventListener('mousemove', onMove);
    c.addEventListener('wheel', onWheel, {passive:false});

    c.addEventListener('touchstart', onTouchStart, {passive:true});
    c.addEventListener('touchmove', onTouchMove, {passive:false});
    c.addEventListener('touchend', onTouchEnd, {passive:true});
    c.addEventListener('touchcancel', onTouchEnd, {passive:true});

    window.addEventListener('resize', () => {
      scheduleViewportFit(0);
      // Keep full wall fitted after orientation/viewport changes
      updatePanelButtonLabel();
    });

    if (window.visualViewport) {
      window.visualViewport.addEventListener('resize', () => scheduleViewportFit(0));
      window.visualViewport.addEventListener('scroll', () => scheduleViewportFit(0));
    }

    // Some browsers change zoom without reliable resize events.
    let lastDpr = window.devicePixelRatio || 1;
    window.setInterval(() => {
      const nowDpr = window.devicePixelRatio || 1;
      if (Math.abs(nowDpr - lastDpr) > 0.0001) {
        lastDpr = nowDpr;
        scheduleViewportFit(0);
      }
    }, 250);

    // Start state: panel visible, full-width overlay; hide/show via panel button or FAB
    if (panel) panel.classList.remove('collapsed');
    updatePanelButtonLabel();

    // quick close gesture
    const hud = document.getElementById('hud');
    if (hud) hud.addEventListener('dblclick', closePanel);
  }

  function init(){
    wire();
    readInputs();
    compute();
    centerView();
    resize();
  }
  init();
})();