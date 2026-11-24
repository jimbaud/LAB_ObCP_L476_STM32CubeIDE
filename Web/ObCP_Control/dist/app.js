/* Interface ObCP. Une session possède son lecteur de trames, sa file d'écriture
   et son transport. Changer de carte invalide les opérations précédentes. */
(() => {
  "use strict";
  const {CHANNELS, setpoint, parseLine, FrameReader, CommandQueue, chooseDevice, openGatt, DemoDevice} = ObCP;
  const $ = id => document.getElementById(id);
  const keys = Object.keys(CHANNELS);
  const inputs = {}, channels = {};
  const prefs = {r: ["LED rouge","--red"], v: ["LED verte","--green"], b: ["LED bleue","--blue"], m: ["Sortie PWM","--pwm"]};
  const demoPercent = {r:65,v:35,b:80,m:50};
  const format = (n, digits) => n.toLocaleString("fr-FR",{minimumFractionDigits:digits,maximumFractionDigits:digits});
  let active = null, connecting = false, busy = false, screen = "pilotage";
  let records = [], logFilter = "all", logScheduled = false, acquisition = "idle", lastSample = 0, seeded = false;
  let note = "", nextConfirmation = null;
  const legends = {acc: [...$("acc-legend").querySelectorAll("b")], temp: [...$("temp-legend").querySelectorAll("b")]};
  const charts = new ObCPCharts($("acc-chart"),$("temp-chart"),(span, following, hasPast)=>{
    $("window-count").textContent = span + " éch.";
    $("older").disabled = !hasPast; $("newer").disabled = following;
    $("follow").disabled = following;
    $("zoom-in").disabled = span<=10; $("zoom-out").disabled = span>=600;
  });

  function textIcon(button, icon, label) {
    button.querySelector("use").setAttribute("href","#i-"+icon);
    button.querySelector("span").textContent = label;
  }
  function showNotice(text) {
    $("notice").hidden = !text;
    $("notice").querySelector("span").textContent = text;
  }
  $("notice").querySelector("button").onclick = () => showNotice("");
  function record(kind, text) {
    records.unshift({at:new Date(),kind,text});
    if (records.length>250) records.pop();
    if (screen==="journal" && !logScheduled) {
      logScheduled=true;
      requestAnimationFrame(()=>{logScheduled=false;renderLog();});
    }
  }
  function renderLog() {
    const list=$("log-list"), fragment=document.createDocumentFragment();
    for (const row of records.filter(entry=>logFilter==="all"||entry.kind===logFilter)) {
      const div=document.createElement("div");div.className="log-row";div.setAttribute("role","listitem");
      const time=document.createElement("time");time.dateTime=row.at.toISOString();time.textContent=row.at.toLocaleTimeString("fr-FR");
      const dir=document.createElement("b");dir.dataset.kind=row.kind;dir.textContent=row.kind;
      const payload=document.createElement("code");payload.textContent=row.text;
      div.append(time,dir,payload);fragment.append(div);
    }
    if (!fragment.childNodes.length) {const p=document.createElement("p");p.textContent="Aucun échange pour ce filtre.";fragment.append(p);}
    list.replaceChildren(fragment);
  }
  function showPage(key = location.hash.slice(1)) {
    const pages={pilotage:["Banc de commande","Pilotage des sorties et acquisition des capteurs par Bluetooth LE."],
      journal:["Journal BLE","Commandes, réponses et événements de la session, du plus récent au plus ancien."],
      guide:["Guide d’utilisation","Connexion, compatibilité et prise en main de la carte ObCP."]};
    screen=Object.hasOwn(pages,key) ? key : "pilotage";
    for (const id of Object.keys(pages)) $(id).hidden=screen!==id;
    document.querySelectorAll("nav a").forEach(a=>{
      if(a.hash==="#"+screen) a.setAttribute("aria-current","page");else a.removeAttribute("aria-current");
    });
    $("page-title").textContent=pages[screen][0];$("page-intro").textContent=pages[screen][1];
    document.title="ObCP Control · "+pages[screen][0];
    if(screen==="journal") renderLog();
    if(screen==="pilotage") charts.refresh();
  }
  // Le fragment est un identifiant de vue, pas une demande de défilement jusqu'aux graphiques.
  function navigate(hash) {
    // Les visionneuses et certaines pages file: bloquent pushState. Le panneau
    // reste utilisable même si ces environnements ne conservent pas son URL.
    try {if(location.hash!==hash) history.pushState(null,"",hash);} catch { /* Historique indisponible. */ }
    showPage(hash.slice(1));window.scrollTo(0,0);
  }
  document.querySelectorAll("nav a,.brand").forEach(a=>a.addEventListener("click",event=>{
    if(event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    navigate(a.hash);
  }));
  window.addEventListener("popstate",()=>showPage());window.addEventListener("hashchange",()=>showPage());

  for (const key of keys) {
    const item=document.createElement("div");item.className="slider-item";item.style.setProperty("--channel","var("+prefs[key][1]+")");
    // Seules des constantes de cette application sont insérées dans ce gabarit.
    item.innerHTML='<div class="slider-top"><label for="range-'+key+'">'+prefs[key][0]+'</label><output class="percent" for="range-'+key+'"></output></div>'+
      '<input id="range-'+key+'" type="range" min="0" max="100" step="1" value="0">'+
      '<div class="slider-bottom"><span></span><code></code></div>';
    $("sliders").append(item);
    const range=item.querySelector("input");
    inputs[key]={range,percent:item.querySelector("output"),hint:item.querySelector(".slider-bottom span"),code:item.querySelector("code")};
    channels[key]={p:0,known:false,state:"",revision:0,timer:null};
    range.addEventListener("input",()=>{
      if(!active || busy || connecting) return;
      const c=channels[key];c.p=Number(range.value);c.known=true;c.state="Réglage";c.revision++;
      renderChannel(key);renderSwatch();
    });
    range.addEventListener("change",()=>scheduleSlider(key));
    // Une carte fraîchement connectée peut déjà avoir une sortie active. Un clic
    // sur le zéro initial doit également permettre d'envoyer explicitement zéro.
    const sendInitial=()=>{if(!channels[key].known) scheduleSlider(key);};
    range.addEventListener("pointerup",sendInitial);
    range.addEventListener("keydown",e=>{if(e.key==="Enter") scheduleSlider(key);});
  }
  function renderChannel(key) {
    const c=channels[key], el=inputs[key], sp=setpoint(key,c.p);
    el.range.value=String(c.p);el.range.style.setProperty("--pct",c.p+"%");
    el.percent.textContent=c.known ? c.p+" %" : "— %";
    el.range.setAttribute("aria-valuetext",c.known ? c.p+" pour cent" : "Consigne inconnue");
    el.hint.textContent=c.state;el.code.textContent=c.known ? sp.command : key+"…";
  }
  function renderSwatch() {
    const known=["r","v","b"].every(k=>channels[k].known);
    $("rgb-preview").querySelector("i").style.background=known ? "rgb("+["r","v","b"].map(k=>Math.round(channels[k].p*2.55)).join(",")+")" : "repeating-linear-gradient(45deg,#ddd 0 2px,#fff 2px 4px)";
  }
  function outputDefaults(type) {
    for (const key of keys) {
      const c=channels[key];clearTimeout(c.timer);c.revision++;
      c.p=type==="demo"?demoPercent[key]:0;c.known=type!=="real";
      c.state=type==="demo"?"Simulé":type==="reset"?"RAZ demandée":"À régler";
      renderChannel(key);
    }
    renderSwatch();
  }
  function scheduleSlider(key) {
    if(!active || busy || connecting) return;
    const session=active,c=channels[key],sp=setpoint(key,Number(inputs[key].range.value));
    c.p=Number(inputs[key].range.value);c.known=true;c.state="À envoyer";
    const revision=++c.revision;
    clearTimeout(c.timer);renderChannel(key);renderSwatch();
    const current=()=>active===session && c.revision===revision;
    c.timer=setTimeout(async()=>{
      if(!current()) return;
      c.state="Envoi…";renderChannel(key);
      try {
        const result=await session.queue.enqueue(sp.command,sp.ack,current);
        if(!current() || result==="cancelled") return;
        c.state=result==="ack"?"Acquitté":"Sans réponse";
        if(result==="timeout") record("INFO",sp.command+" · acquittement absent après 5 essais.");
      } catch(e) {
        if(!current()) return;
        c.state="Échec";commandError(e);
      }
      renderChannel(key);
    },100);
  }
  function setNote(text) {note=text;$("outputs-note").textContent=note;}
  function streamState() {
    const el=$("stream-state");
    let text;let receiving=false;
    if(!active) text="Hors ligne";
    else if(active.type==="demo") {receiving=acquisition==="running";text=receiving?"Simulation en cours":"Simulation arrêtée";}
    else if(acquisition==="stopped") text="Arrêt demandé";
    else if(acquisition==="running" && lastSample && Date.now()-lastSample<3600) {text="Mesures reçues";receiving=true;}
    else if(acquisition==="running") text=lastSample?"En attente de mesures":"Démarrage demandé";
    else text="Prête à démarrer";
    if(el.textContent!==text) el.textContent=text;
    el.dataset.on=String(receiving);
  }
  function renderControls() {
    const demo=active?.type==="demo", real=active?.type==="real";
    const badge=$("link-state");badge.className="badge"+(real?" live":demo?"":" off");
    textIcon(badge,real?"radio":demo?"flask":"bt",real?active.name:demo?"Mode démo":"Hors ligne");
    textIcon($("connect"),"bt",connecting?"Connexion…":real?"Déconnecter":"Connecter une carte");
    $("connect").disabled=connecting;
    $("demo").disabled=demo||connecting;$("demo").querySelector("span").textContent=demo?"Démonstration active":"Activer la démonstration";
    for(const key of keys) inputs[key].range.disabled=!active||connecting||busy;
    $("reset").disabled=!active||connecting||busy;$("timer").disabled=!active||connecting||busy;
    textIcon($("timer"),acquisition==="running"?"stop":"play",acquisition==="running"?"Arrêter l’acquisition":"Démarrer l’acquisition");
    streamState();
  }
  function clearGraphs() {
    charts.clear();
    Object.values(legends).flat().forEach(el=>el.textContent="—");
  }
  function dispatch(session,line) {
    if(active!==session) return;
    const parsed=parseLine(line);
    if(!seeded) record("RX",line);
    session.queue.receive(parsed);
    if(parsed.kind!=="acc" && parsed.kind!=="temp") return;
    charts.add(parsed.kind,parsed.values);
    parsed.values.forEach((v,i)=>{legends[parsed.kind][i].textContent=format(v,parsed.kind==="acc"?2:1);});
    if(session.type==="real"){
      lastSample=Date.now();
      if(acquisition==="idle"){acquisition="running";renderControls();}else streamState();
    }
  }
  function makeSession(type,name) {
    const session={type,name,link:null,queue:null,open:false,pending:[],lost:false};
    session.reader=new FrameReader(line=>{
      if(session.open) dispatch(session,line);
      else if(!session.lost && session.pending.length<32) session.pending.push(line);
    });
    return session;
  }
  function retire(session) {
    if(!session) return;
    session.open=false;session.lost=true;session.reader.reset();session.queue?.close();session.link?.close();
    for (const key of keys){const c=channels[key];clearTimeout(c.timer);c.revision++;}
  }
  function activate(session) {
    const previous=active;active=null;retire(previous);
    session.queue=new CommandQueue(command=>session.link.write(command),(command,attempt)=>record("TX",command+(attempt>1?" · essai "+attempt:"")));
    active=session;session.open=true;busy=false;lastSample=0;
    records=[];clearGraphs();charts.zoom(60);acquisition=session.type==="demo"?"running":"idle";
    outputDefaults(session.type);
    setNote(session.type==="demo"?"Consignes simulées sur 16 bits.":"Les consignes sont envoyées au relâchement.");
    showNotice("");record("INFO",session.type==="demo"?"Ouverture du simulateur.":"Notifications activées sur "+session.name+".");
    for(const line of session.pending) dispatch(session,line);
    session.pending=[];renderControls();
    if(screen==="journal") renderLog();
  }
  function startDemo() {
    const session=makeSession("demo","Simulateur ObCP");
    session.link=new DemoDevice(text=>session.reader.feed(text));
    activate(session);
    // L'historique de départ est formé exclusivement de mesures simulées.
    seeded=true;
    try {for(let i=0;i<72;i++) session.link.sample();}finally{seeded=false;}
    record("INFO","72 échantillons simulés préchargés. Acquisition à 1 Hz.");
    session.link.start();renderControls();
  }
  function disconnect(message) {
    const previous=active;if(!previous) return;
    active=null;retire(previous);busy=false;acquisition="idle";lastSample=0;
    for (const key of keys) {channels[key].state="Hors ligne";renderChannel(key);}
    setNote("Dernières consignes de la session terminée.");
    record("INFO",message);showNotice(message+" Les courbes restent consultables.");renderControls();
  }
  function commandError(error) {
    const text="Échec de l’écriture BLE. "+(error?.message||"Vérifiez la liaison.");
    record("INFO",text);showNotice(text);
  }
  function connectionError(error, selecting) {
    if(error.name==="NotFoundError" && selecting) return "Aucune carte sélectionnée.";
    if(error.name==="NotFoundError") return "Service ou caractéristique ObCP introuvable. Vérifiez le firmware et le périphérique sélectionné.";
    if(error.name==="SecurityError" || error.name==="NotAllowedError") return "Autorisation Bluetooth refusée. Ouvrez la page HTTPS dans un onglet indépendant et vérifiez les autorisations Bluetooth.";
    if(error.name==="NetworkError") return "Liaison BLE interrompue. Vérifiez l’alimentation, déconnectez l’application Android puis réessayez.";
    return "Connexion impossible. "+(error.message||String(error));
  }
  $("connect").onclick=async()=>{
    if(connecting) return;
    if(active?.type==="real"){disconnect("Carte déconnectée.");return;}
    if(!isSecureContext){showNotice("Le BLE exige une page HTTPS ou localhost. La démonstration reste disponible.");return;}
    if(!navigator.bluetooth?.requestDevice){showNotice("Ce navigateur ne fournit pas Web Bluetooth. Utilisez Chrome ou Edge sur ordinateur, Chrome sur Android, ou essayez Bluefy sur iOS. Le guide détaille la connexion.");return;}
    let selecting=true,candidate=null;
    connecting=true;showNotice("");renderControls();
    try {
      // Appel direct depuis le clic : le navigateur exige une activation utilisateur.
      const device=await chooseDevice(navigator.bluetooth,$("prefix").value.trim());
      selecting=false;
      candidate=makeSession("real",device.name||"Carte ObCP");
      const decoder=new TextDecoder();
      candidate.link=await openGatt(device,value=>candidate.reader.feed(decoder.decode(value)),
        ()=>{candidate.lost=true;if(active===candidate) disconnect("Liaison Bluetooth perdue.");});
      if(candidate.lost) throw new Error("La carte s’est déconnectée pendant l’initialisation.");
      activate(candidate);
    } catch(e) {
      if(candidate){candidate.reader.reset();candidate.link?.close();}
      const message=connectionError(e,selecting);record("INFO",message);showNotice(message);
    } finally {connecting=false;renderControls();}
  };
  $("demo").onclick=()=>{
    if(!active){startDemo();navigate("#pilotage");return;}
    if(active.type==="real") confirmAction("Passer en démonstration","La liaison BLE sera fermée et les courbes seront remplacées par les données simulées.","Passer en démo",()=>{startDemo();navigate("#pilotage");});
  };
  function confirmAction(title,body,yes,action) {
    const dialog=$("confirm");
    $("confirm-title").textContent=title;$("confirm-text").textContent=body;$("confirm-yes").textContent=yes;
    if(typeof dialog.showModal!=="function"){if(window.confirm(body)) action();return;}
    dialog.returnValue="cancel";nextConfirmation=action;dialog.showModal();
  }
  $("confirm").addEventListener("close",()=>{
    const action=nextConfirmation;nextConfirmation=null;
    if($("confirm").returnValue==="ok" && action) action();
  });
  async function runControl(command,success) {
    const session=active;if(!session || busy) return;
    busy=true;renderControls();
    try {
      const result=await session.queue.enqueue(command);
      if(active===session && result==="sent") success();
    } catch(e) {if(active===session) commandError(e);}
    finally{if(active===session){busy=false;renderControls();}}
  }
  $("timer").onclick=()=>{
    const start=acquisition!=="running", previousSample=lastSample;
    runControl(start?"start timer":"stop timer",()=>{
      acquisition=start?"running":"stopped";
      if(!start || lastSample===previousSample) lastSample=0;
    });
  };
  $("reset").onclick=()=>{
    const session=active;if(!session)return;
    confirmAction("Remettre les sorties à zéro ?","La LED RVB et la sortie PWM seront éteintes. L’acquisition s’arrêtera et les courbes resteront à l’écran.","Remettre à zéro",()=>{
      if(active!==session) return;
      for(const c of Object.values(channels)){clearTimeout(c.timer);c.revision++;}
      runControl("#rst",()=>{
        outputDefaults("reset");acquisition="stopped";lastSample=0;
        setNote(session.type==="demo"?"Sorties simulées à zéro.":"Commande #rst envoyée, sans acquittement prévu.");
      });
    });
  };
  $("clear").onclick=()=>{clearGraphs();record("INFO","Historiques des courbes effacés.");};
  $("clear-log").onclick=()=>{records=[];renderLog();};
  for(const btn of document.querySelectorAll("[data-log-filter]")) btn.onclick=()=>{
    logFilter=btn.dataset.logFilter;
    document.querySelectorAll("[data-log-filter]").forEach(el=>el.setAttribute("aria-pressed",String(el===btn)));renderLog();
  };
  $("zoom-in").onclick=()=>charts.zoom(charts.span/1.5);$("zoom-out").onclick=()=>charts.zoom(charts.span*1.5);
  $("older").onclick=()=>charts.pan(-Math.max(1,Math.round(charts.span/2)));
  $("newer").onclick=()=>charts.pan(Math.max(1,Math.round(charts.span/2)));$("follow").onclick=()=>charts.follow();
  showPage();startDemo();setInterval(streamState,1000);
  if (!document.documentElement.hasAttribute("data-portable") && "serviceWorker" in navigator &&
      isSecureContext && /^https?:$/.test(location.protocol)) {
    navigator.serviceWorker.register("./sw.js").catch(()=>{record("INFO","Cache hors ligne indisponible dans ce navigateur.");});
  }
})();
