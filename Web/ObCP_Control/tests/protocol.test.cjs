"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const {setpoint, parseLine, FrameReader, CommandQueue, chooseDevice, UUID, openGatt, DemoDevice} = require("../dist/protocol.js");
const sleep = ms => new Promise(resolve=>setTimeout(resolve,ms));
const encode = str => new TextEncoder().encode(str);
const decode = bytes => new TextDecoder().decode(bytes);

// toInt() de Kotlin tronque une opération flottante positive.
test("les 404 consignes entières correspondent aux calculs Float du code Kotlin",()=>{
  const f=Math.fround;
  for(const key of ["r","v","b","m"]) for(let p=0;p<=100;p++){
    const ratio=f(p/100), gain=key==="m"?ratio:f(ratio**3);
    const expected=Math.trunc(f(65535*gain));
    assert.equal(setpoint(key,p).value,expected);
    assert.equal(setpoint(key,p).command,key+expected);
  }
  assert.equal(setpoint("v",50).ack,"Green = 8191");
  assert.equal(setpoint("m",50).command,"m32767");
  for(const n of [-1,101,NaN]) assert.throws(()=>setpoint("r",n),RangeError);
});

test("fragmentation à chaque offset, LF CR, CR LF, lignes groupées et 20 octets",()=>{
  const lines=["X-1.01Y-0.99Z-0.53","T = -12.5","Green = 65535","PButton pressed"];
  const stream=lines.join("\n\r")+"\r\n";
  for(let split=0;split<=stream.length;split++){
    const got=[],reader=new FrameReader(x=>got.push(x));
    reader.feed(stream.slice(0,split));reader.feed(stream.slice(split));reader.reset();
    assert.deepEqual(got,lines);
  }
  const got=[], reader=new FrameReader(x=>got.push(parseLine(x)));
  for(let i=0;i<stream.length;i+=20)reader.feed(stream.slice(i,i+20));
  assert.deepEqual(got[0],{kind:"acc",values:[-1.01,-.99,-.53]});
  assert.deepEqual(got[1],{kind:"temp",values:[-12.5]});
  assert.deepEqual(got[2],{kind:"ack",text:"Green = 65535"});
  assert.deepEqual(got[3],{kind:"message",text:"PButton pressed"});reader.reset();
});

test("les lignes incomplètes, invalides ou trop longues ne deviennent pas des mesures",()=>{
  const out=[],r=new FrameReader(line=>out.push(parseLine(line)));
  r.feed("X0.12Y3\nT = 25a\n"+"x".repeat(800)+"\nT = 28\n");
  assert.deepEqual(out.map(x=>x.kind),["message","message","temp"]);
  assert.equal(parseLine("X1e999Y2Z3").kind,"message");
  assert.equal(parseLine("Blue = 90000").kind,"message");
  r.reset();
});

test("compatibilité avec des notifications anciennes sans fin de ligne", async()=>{
  const got=[], r=new FrameReader(x=>got.push(x),10);
  r.feed("T = 2");r.feed("5");r.feed("Red = 8191");
  await sleep(20);assert.deepEqual(got,["T = 25","Red = 8191"]);
  r.feed("T = 3");r.reset();await sleep(20);assert.equal(got.length,2);
});

test("découverte sans UUID annoncé et recherche par préfixe",async()=>{
  const log=[],bt={requestDevice(opts){log.push(opts);return Promise.resolve({});}};
  await chooseDevice(bt);await chooseDevice(bt,"OBCP");
  assert.deepEqual(log,[{acceptAllDevices:true,optionalServices:[UUID.service]},
    {filters:[{namePrefix:"OBCP"}],optionalServices:[UUID.service]}]);
});

// Un périphérique factice enregistre uniquement les primitives utilisées par le navigateur.
function peripheral(variant="native") {
  const calls=[],device=new EventTarget(),rx=new EventTarget(),tx={};
  device.name="OBCP-CROC";
  rx.properties={notify:variant!=="reversed"};
  rx.startNotifications=async()=>{calls.push("notify");if(variant==="notify-error") throw Error("Échec de souscription");};
  tx.properties=variant==="response"?{write:true}:{writeWithoutResponse:true};
  const write=type=>async bytes=>{calls.push([type,[...bytes]]);};
  if(variant==="legacy") tx.writeValue=write("legacy");
  else if(variant==="response") tx.writeValueWithResponse=write("response");
  else tx.writeValueWithoutResponse=write("withoutResponse");
  const service={getCharacteristic:async id=>{
    calls.push(id);
    if(id===UUID.notify)return rx;if(id===UUID.write)return tx;throw Error("UUID inattendu");
  }};
  device.gatt={connected:false,
    async connect(){this.connected=true;calls.push("connect");return this;},
    async getPrimaryService(id){assert.equal(id,UUID.service);calls.push(id);return service;},
    disconnect(){this.connected=false;calls.push("disconnect");device.dispatchEvent(new Event("gattserverdisconnected"));}
  };
  return {device,calls,notify(text){const a=encode(text);rx.value=new DataView(a.buffer);rx.dispatchEvent(new Event("characteristicvaluechanged"));}};
}

test("découverte, souscription puis écritures ASCII …0003, sans délimiteur",async()=>{
  for(const [variant,method] of [["native","withoutResponse"],["legacy","legacy"],["response","response"]]) {
    const p=peripheral(variant),seen=[];let drops=0;
    const link=await openGatt(p.device,data=>seen.push(decode(data)),()=>drops++);
    assert.deepEqual(p.calls,["connect",UUID.service,UUID.notify,UUID.write,"notify"]);
    for(const cmd of ["r8191","start timer","stop timer","#rst"]) await link.write(cmd);
    assert.deepEqual(p.calls.slice(5),["r8191","start timer","stop timer","#rst"].map(cmd=>[method,[...encode(cmd)]]));
    p.notify("T = 21\n\r");assert.deepEqual(seen,["T = 21\n\r"]);
    link.close();p.notify("T = 88");assert.equal(seen.length,1);assert.equal(drops,0);
  }
});

test("profil inversé et échec de souscription libèrent la connexion",async()=>{
  for(const v of ["reversed","notify-error"]){
    const p=peripheral(v);await assert.rejects(openGatt(p.device,()=>{},()=>{}));
    assert.equal(p.device.gatt.connected,false);assert.equal(p.calls.filter(x=>x==="disconnect").length,1);
  }
});

test("perte de liaison réelle, retrait des écouteurs et invalidation de l'ancienne écriture",async()=>{
  const p=peripheral();let drops=0,lines=0;
  const l=await openGatt(p.device,()=>lines++,()=>drops++);
  p.device.gatt.disconnect();p.notify("late");assert.equal(lines,0);assert.equal(drops,1);
  await assert.rejects(l.write("b0"));l.close();assert.equal(drops,1);
});

test("ACK immédiat, ACK obsolète, télémesure intercalée et ordre des curseurs",async()=>{
  let attempts=0;const writes=[];
  const q=new CommandQueue(async cmd=>{
    writes.push(cmd);
    if(cmd==="r8191" && ++attempts===1){q.receive(parseLine("Green = 8191"));q.receive(parseLine("T = 23"));}
    else if(cmd==="r8191")q.receive(parseLine("Red = 8191"));
    else if(cmd==="v0")q.receive(parseLine("Green = 0"));
  },()=>{},{timeout:5});
  q.receive(parseLine("Red = 8191"));
  assert.deepEqual(await Promise.all([q.enqueue("r8191","Red = 8191"),q.enqueue("v0","Green = 0"),q.enqueue("start timer")]),["ack","ack","sent"]);
  assert.deepEqual(writes,["r8191","r8191","v0","start timer"]);q.close();
});

test("cinq essais bornés, commande remplacée ignorée, pas de répétition d'un reset",async()=>{
  const writes=[];const q=new CommandQueue(async cmd=>writes.push(cmd),()=>{},{timeout:3});
  assert.equal(await q.enqueue("r0","Red = 0"),"timeout");
  assert.equal(await q.enqueue("r500","Red = 500",()=>false),"cancelled");
  assert.equal(await q.enqueue("#rst"),"sent");
  assert.deepEqual(writes,["r0","r0","r0","r0","r0","#rst"]);q.close();
});

test("fermer une session annule l'attente et les écritures déjà en file",async()=>{
  let written;const started=new Promise(resolve=>{written=resolve;});const log=[];
  const q=new CommandQueue(async cmd=>{log.push(cmd);written();},()=>{},{timeout:1000});
  const one=q.enqueue("b1","Blue = 1"),two=q.enqueue("r1","Red = 1");
  await started;q.close();
  assert.deepEqual(await Promise.all([one,two]),["cancelled","cancelled"]);assert.deepEqual(log,["b1"]);
});

test("les mesures et acquittements du simulateur utilisent le parseur du matériel",async()=>{
  const got=[],reader=new FrameReader(x=>got.push(parseLine(x)));
  const demo=new DemoDevice(text=>reader.feed(text));demo.sample();
  await demo.write("m32767");await demo.write("start timer");await demo.write("stop timer");await demo.write("#rst");
  assert.deepEqual(got.map(x=>x.kind),["acc","temp","ack"]);
  assert.equal(got[2].text,"PWM = 32767");assert.deepEqual(demo.outputs,{r:0,v:0,b:0,m:0});
  demo.close();reader.reset();await assert.rejects(demo.write("b0"));
});
