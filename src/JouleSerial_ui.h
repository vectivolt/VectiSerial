// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------
//
// JouleSerial wireless console UI. Single-page app. Live log over the
// /serial/ws WebSocket. Mobile-friendly, theme-aware, keyboard-driven.
//
// Premium features baked in:
//   * Live level filter (DEBUG/INFO/WARN/ERROR) with color tints
//   * Regex search with highlight on matches
//   * Auto-scroll lock + manual pause
//   * Timestamp toggle (off / relative-ms / absolute-wall)
//   * Hex / ASCII view toggle
//   * Font size + family controls (saved in localStorage)
//   * Export to TXT / JSON / CSV (filtered set, not the full ring)
//   * Command input with up/down history (last 50)
//   * Live rate counter (lines/sec) + per-level totals
//   * Multi-client indicator
#pragma once
#include <Arduino.h>

namespace joule {
static const char SERIAL_UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="theme-color" content="#0a0c12"/>
<title>__TITLE__</title>
<style>
:root{
  --bg:#0a0c12;--panel:rgba(255,255,255,.04);--panel-s:#141a2a;
  --ink:#e8ecf5;--ink2:#a5acc1;--muted:#6b7390;--line:rgba(255,255,255,.08);
  --brand:__BRAND__;--brand-2:#22d3ee;
  --dbg:#7aa6ff;--inf:#3ddc97;--wrn:#ffb347;--err:#ff6b81;
  --r:14px;--shadow:0 10px 30px rgba(0,0,0,.3);
  --grad:linear-gradient(135deg,var(--brand),var(--brand-2));
  --fs:13px;
}
:root[data-theme="light"]{--bg:#f4f6fc;--panel:rgba(255,255,255,.75);--panel-s:#fff;--ink:#0f1730;--ink2:#3a4366;--line:rgba(15,23,48,.08);--shadow:0 10px 28px rgba(20,32,80,.08)}
@media(prefers-color-scheme:light){:root[data-theme="auto"]{--bg:#f4f6fc;--panel:rgba(255,255,255,.75);--panel-s:#fff;--ink:#0f1730;--ink2:#3a4366;--line:rgba(15,23,48,.08);--shadow:0 10px 28px rgba(20,32,80,.08)}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}html,body{margin:0;height:100%;overflow:hidden}
body{font:13.5px/1.45 -apple-system,BlinkMacSystemFont,"Inter","Segoe UI",Roboto,sans-serif;color:var(--ink);background:var(--bg);
  background-image:radial-gradient(900px 400px at 100% 0,color-mix(in srgb,var(--brand) 12%,transparent),transparent 60%)}
header{display:flex;align-items:center;gap:10px;padding:10px 14px;border-bottom:1px solid var(--line);backdrop-filter:saturate(140%) blur(14px);-webkit-backdrop-filter:saturate(140%) blur(14px);background:color-mix(in srgb,var(--bg) 80%,transparent);position:sticky;top:0;z-index:5}
.logo{width:30px;height:30px;border-radius:9px;background:var(--grad);display:grid;place-items:center;color:#fff;font-weight:800;font-size:13px}
.title{font-weight:700;font-size:14px}
.spacer{flex:1}
.dot{width:7px;height:7px;border-radius:50%;background:var(--muted)}.dot.on{background:#3ddc97;box-shadow:0 0 0 3px color-mix(in srgb,#3ddc97 25%,transparent)}
.bar{display:flex;align-items:center;gap:6px;padding:8px 14px;border-bottom:1px solid var(--line);flex-wrap:wrap;background:color-mix(in srgb,var(--bg) 80%,transparent)}
.btn{padding:6px 10px;border-radius:8px;border:1px solid var(--line);background:var(--panel);color:var(--ink);cursor:pointer;font:inherit;font-size:12px;transition:.15s;min-height:34px;display:inline-flex;align-items:center;gap:6px}
.btn:hover{border-color:var(--brand);color:var(--brand)}
.btn.solid{background:var(--grad);color:#fff;border-color:transparent;font-weight:700}
.btn.active{background:color-mix(in srgb,var(--brand) 20%,transparent);border-color:var(--brand);color:var(--brand)}
select,input{background:var(--panel);color:var(--ink);border:1px solid var(--line);border-radius:8px;padding:6px 10px;font:inherit;font-size:12px;min-height:34px;outline:none;transition:.15s}
select:focus,input:focus{border-color:var(--brand);box-shadow:0 0 0 2px color-mix(in srgb,var(--brand) 18%,transparent)}
input.search{min-width:200px}
.chip{font-size:11px;color:var(--ink2);padding:3px 8px;border-radius:99px;background:var(--panel);border:1px solid var(--line);font-family:"SF Mono",ui-monospace,monospace}
.chip b{color:var(--ink);font-weight:700}
.log{position:absolute;inset:auto 0 56px 0;top:96px;overflow:auto;padding:6px 14px;font-family:"SF Mono",ui-monospace,Menlo,Consolas,monospace;font-size:var(--fs);background:color-mix(in srgb,var(--bg) 50%,transparent)}
.log:focus{outline:none}
.line{display:grid;grid-template-columns:84px 50px 1fr;gap:10px;padding:2px 6px;border-radius:6px;transition:.1s}
.line:hover{background:color-mix(in srgb,var(--ink) 4%,transparent)}
.line .ts{color:var(--muted);font-size:11px;align-self:center}
.line .lvl{font-weight:800;font-size:10px;text-align:center;padding:2px 6px;border-radius:5px;align-self:center;letter-spacing:.4px}
.line .lvl.dbg{color:#fff;background:color-mix(in srgb,var(--dbg) 60%,transparent)}
.line .lvl.inf{color:#fff;background:color-mix(in srgb,var(--inf) 60%,transparent)}
.line .lvl.wrn{color:#000;background:color-mix(in srgb,var(--wrn) 80%,transparent)}
.line .lvl.err{color:#fff;background:color-mix(in srgb,var(--err) 70%,transparent)}
.line .txt{color:var(--ink);word-break:break-word;white-space:pre-wrap}
.hide-ts .ts{display:none}.hide-ts .line{grid-template-columns:50px 1fr}
.foot{position:absolute;left:0;right:0;bottom:0;padding:10px 14px;border-top:1px solid var(--line);backdrop-filter:saturate(140%) blur(14px);-webkit-backdrop-filter:saturate(140%) blur(14px);background:color-mix(in srgb,var(--bg) 85%,transparent);display:flex;gap:8px;align-items:center}
.foot input.cmd{flex:1;font-family:"SF Mono",ui-monospace,monospace;min-height:40px;font-size:13px}
.match{background:color-mix(in srgb,var(--wrn) 40%,transparent);border-radius:3px;padding:0 2px}
@media(max-width:760px){.bar{padding:8px}.chip{display:none}.log{top:110px}}
@media(max-width:520px){.bar input.search{flex:1;min-width:0}}
</style></head><body>
<header>
  <div class="logo">⌨</div>
  <div><div class="title">__TITLE__</div><div class="chip" style="margin-top:2px"><span class="dot" id="wsDot"></span> <span id="wsTxt">disconnected</span> · <b id="clients">0</b> clients</div></div>
  <div class="spacer"></div>
  <button class="btn" id="themeBtn" title="theme">◐</button>
</header>
<div class="bar" id="topbar">
  <input class="search" id="search" placeholder="filter (regex ok) — press /"/>
  <select id="levelSel">
    <option value="0">DEBUG+</option><option value="1" selected>INFO+</option>
    <option value="2">WARN+</option><option value="3">ERROR</option>
  </select>
  <button class="btn active" id="autoBtn" title="autoscroll">▼ Auto</button>
  <button class="btn" id="tsBtn" title="timestamps">⏱ rel</button>
  <button class="btn" id="hexBtn" title="hex view">𝟬𝟭 text</button>
  <select id="fontSel" title="font size"><option value="12">12px</option><option value="13" selected>13px</option><option value="14">14px</option><option value="16">16px</option></select>
  <select id="expSel" title="export"><option value="">⤓ export</option><option value="txt">Text</option><option value="json">JSON</option><option value="csv">CSV</option></select>
  <button class="btn" id="clearBtn" title="clear">✕ Clear</button>
  <span class="chip">dbg <b id="cntDbg">0</b></span>
  <span class="chip">inf <b id="cntInf">0</b></span>
  <span class="chip">wrn <b id="cntWrn">0</b></span>
  <span class="chip">err <b id="cntErr">0</b></span>
  <span class="chip">rate <b id="cntRate">0</b>/s</span>
  <span class="chip">total <b id="cntLines">0</b></span>
</div>
<div class="log" id="log" tabindex="0"></div>
<div class="foot">
  <input class="cmd" id="cmd" placeholder="type a command — Enter to send · ↑/↓ history · / to focus filter" autocomplete="off" autofocus/>
  <button class="btn solid" id="sendBtn">Send ↵</button>
</div>
<script>
const $=s=>document.querySelector(s);
const log=$("#log"),search=$("#search"),levelSel=$("#levelSel"),autoBtn=$("#autoBtn"),tsBtn=$("#tsBtn"),hexBtn=$("#hexBtn"),clearBtn=$("#clearBtn"),expSel=$("#expSel"),fontSel=$("#fontSel"),cmd=$("#cmd"),wsDot=$("#wsDot"),wsTxt=$("#wsTxt");
let lines=[],ws=null,autoscroll=true,tsMode="rel",hex=false,cnt={0:0,1:0,2:0,3:0},rateBuf=[],hist=[],histI=-1;

function applyTheme(){document.documentElement.setAttribute("data-theme",localStorage.getItem("joule-theme")||"auto")}
$("#themeBtn").onclick=()=>{const c=document.documentElement.getAttribute("data-theme")||"auto";const n=c==="auto"?"dark":(c==="dark"?"light":"auto");localStorage.setItem("joule-theme",n);applyTheme()};
applyTheme();

const savedFs=localStorage.getItem("joule-fs");if(savedFs){fontSel.value=savedFs;document.documentElement.style.setProperty("--fs",savedFs+"px")}
fontSel.onchange=()=>{document.documentElement.style.setProperty("--fs",fontSel.value+"px");localStorage.setItem("joule-fs",fontSel.value)};

function setStatus(on,t){wsDot.className="dot"+(on?" on":"");wsTxt.textContent=t}
function open(){
  const proto=location.protocol==="https:"?"wss:":"ws:";
  ws=new WebSocket(proto+"//"+location.host+"/serial/ws");
  ws.onopen=()=>setStatus(true,"online");
  ws.onclose=()=>{setStatus(false,"offline · retry");setTimeout(open,1500)};
  ws.onmessage=ev=>{try{handle(JSON.parse(ev.data))}catch(e){}};
}
function handle(m){
  if(m.type==="hist"){m.lines.forEach(addLine);render()}
  else if(m.type==="line"){addLine(m);render(true)}
  else if(m.type==="clients"){$("#clients").textContent=m.n}
}
function addLine(l){lines.push(l);if(lines.length>5000)lines.shift();cnt[l.lvl]=(cnt[l.lvl]||0)+1;rateBuf.push(Date.now())}

const LV=["DBG","INF","WRN","ERR"],LC=["dbg","inf","wrn","err"];
function fmtTs(l){if(tsMode==="off")return"";if(tsMode==="rel"){const s=l.ms/1000;return s.toFixed(3)+"s"}return new Date(Date.now()-(performance.now()-l.ms)).toISOString().substr(11,12)}
function fmtTxt(t){if(!hex)return t;let o="";for(let i=0;i<t.length;i++)o+=t.charCodeAt(i).toString(16).padStart(2,"0")+" ";return o.trim()}

function render(append){
  const minLvl=parseInt(levelSel.value),q=search.value.trim();let re=null;
  if(q){try{re=new RegExp(q,"i")}catch(e){re=null}}
  const filter=l=>l.lvl>=minLvl&&(!re||re.test(l.text));
  const visible=lines.filter(filter);
  log.classList.toggle("hide-ts",tsMode==="off");
  log.innerHTML=visible.slice(-2000).map(l=>{
    const txt=fmtTxt(l.text);const safe=txt.replace(/[<>&]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;'}[c]));
    const hi=re?safe.replace(re,m=>`<span class="match">${m}</span>`):safe;
    return `<div class="line"><span class="ts">${fmtTs(l)}</span><span class="lvl ${LC[l.lvl]}">${LV[l.lvl]}</span><span class="txt">${hi}</span></div>`;
  }).join("");
  $("#cntLines").textContent=visible.length;
  $("#cntDbg").textContent=cnt[0]||0;$("#cntInf").textContent=cnt[1]||0;
  $("#cntWrn").textContent=cnt[2]||0;$("#cntErr").textContent=cnt[3]||0;
  const cutoff=Date.now()-1000;rateBuf=rateBuf.filter(t=>t>cutoff);$("#cntRate").textContent=rateBuf.length;
  if(autoscroll||append)log.scrollTop=log.scrollHeight;
}
search.oninput=()=>render();levelSel.onchange=()=>render();
autoBtn.onclick=()=>{autoscroll=!autoscroll;autoBtn.classList.toggle("active",autoscroll);autoBtn.textContent=(autoscroll?"▼ Auto":"⏸ Hold");if(autoscroll)log.scrollTop=log.scrollHeight};
tsBtn.onclick=()=>{tsMode=tsMode==="rel"?"abs":(tsMode==="abs"?"off":"rel");tsBtn.textContent="⏱ "+tsMode;render()};
hexBtn.onclick=()=>{hex=!hex;hexBtn.textContent=hex?"𝟬𝟭 hex":"𝟬𝟭 text";hexBtn.classList.toggle("active",hex);render()};
clearBtn.onclick=()=>{lines=[];cnt={0:0,1:0,2:0,3:0};render()};
expSel.onchange=()=>{
  const v=expSel.value;if(!v)return;expSel.value="";
  const minLvl=parseInt(levelSel.value),q=search.value.trim();let re=null;if(q){try{re=new RegExp(q,"i")}catch(e){}}
  const data=lines.filter(l=>l.lvl>=minLvl&&(!re||re.test(l.text)));
  let blob;
  if(v==="json")blob=new Blob([JSON.stringify(data,null,2)],{type:"application/json"});
  else if(v==="csv"){const rows=["seq,ms,level,text"].concat(data.map(l=>`${l.seq},${l.ms},${LV[l.lvl]},"${l.text.replace(/"/g,'""')}"`));blob=new Blob([rows.join("\n")],{type:"text/csv"})}
  else blob=new Blob([data.map(l=>`[${fmtTs(l)}] ${LV[l.lvl]} ${l.text}`).join("\n")],{type:"text/plain"});
  const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download="serial-"+Date.now()+"."+v;a.click();
};
log.addEventListener("scroll",()=>{const atBottom=log.scrollHeight-log.scrollTop-log.clientHeight<20;if(!atBottom&&autoscroll){autoscroll=false;autoBtn.classList.remove("active");autoBtn.textContent="⏸ Hold"}});
cmd.addEventListener("keydown",e=>{
  if(e.key==="Enter"){const t=cmd.value;if(!t)return;if(ws&&ws.readyState===1)ws.send(JSON.stringify({type:"cmd",text:t}));hist.unshift(t);if(hist.length>50)hist.pop();histI=-1;cmd.value=""}
  else if(e.key==="ArrowUp"){if(histI<hist.length-1){histI++;cmd.value=hist[histI];setTimeout(()=>cmd.setSelectionRange(cmd.value.length,cmd.value.length),0)}}
  else if(e.key==="ArrowDown"){if(histI>0){histI--;cmd.value=hist[histI]}else if(histI===0){histI=-1;cmd.value=""}}
});
$("#sendBtn").onclick=()=>{cmd.dispatchEvent(new KeyboardEvent("keydown",{key:"Enter"}))};
document.addEventListener("keydown",e=>{if(e.key==="/"&&document.activeElement!==cmd&&document.activeElement!==search){e.preventDefault();search.focus()}});
setInterval(render,1000);open();
</script></body></html>)HTML";
} // namespace joule
