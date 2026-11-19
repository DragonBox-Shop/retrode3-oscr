#!/usr/bin/env node
'use strict';
const fs=require('node:fs'),path=require('node:path');
function extractSymbols(s){
  const found={};
  for(const m of s.matchAll(/constexpr\s+char\s+const\s+PROGMEM\s+(\w+)\s*\[\]\s*=\s*"((?:\\.|[^"\\])*)"\s*;/g))found[m[1]]=JSON.parse('"'+m[2]+'"');
  return found;
}
function conditionalTokens(body){
  const result=[],flags=[];
  for(const raw of body.split('\n')){
    const line=raw.replace(/\/\/.*$/,'').trim();if(!line)continue;
    let m=line.match(/^#\s*if\s+(.*)$/);
    if(m){flags.push(m[1].trim());continue;}
    m=line.match(/^#\s*ifdef\s+(\w+)/);
    if(m){flags.push('defined('+m[1]+')');continue;}
    m=line.match(/^#\s*ifndef\s+(\w+)/);
    if(m){flags.push('!defined('+m[1]+')');continue;}
    if(/^#\s*else\b/.test(line)){if(!flags.length)throw Error('unmatched #else');flags[flags.length-1]='!('+flags[flags.length-1]+')';continue;}
    if(/^#\s*elif\b/.test(line))throw Error('unsupported #elif');
    if(/^#\s*endif\b/.test(line)){if(!flags.length)throw Error('unmatched #endif');flags.pop();continue;}
    for(const part of line.split(',')){
      const token=part.trim().replace(/\s*=\s*\d+\s*$/,'');
      if(token)result.push({token,requires:[...flags]});
    }
  }
  if(flags.length)throw Error('unterminated #if');
  return result;
}
function kebab(x){return x.replace(/([a-z0-9])([A-Z])/g,'$1-$2').replace(/([A-Z])([A-Z][a-z])/g,'$1-$2').replace(/_/g,'-').toLowerCase();}
function extractCatalog(coreSources,localeSource,db){
  const block=localeSource.match(/namespace MenuOptions[^{]*\{([\s\S]*?)\n  \}/);
  if(!block)throw Error('Missing menu localizations');
  const english=extractSymbols(block[1]),out=[];
  const menuMap={
    NES:{menuOptions:'MenuOption',menuOptionsNESChips:null,menuOptionsNESFlash:null},
    MegaDrive:{menuOptionsMD:'MDMenuOption',menuOptionsMDCart:'MDCartMenuOption',menuOptionsSCD:'MDRAMMenuOption'},
    SNES:{menuOptions:'MenuOption',menuOptionsSNES:'SNESMenuOption',menuOptionsConfManual:null,menuOptionsRepro:null,menuOptionsReproCFI:null}
  };
  for(const [system,src] of Object.entries(coreSources)){
    const labels=extractSymbols(src),enums={},arrays={};
    for(const m of src.matchAll(/enum\s+class\s+(\w+)\s*:\s*\w+\s*\{([\s\S]*?)\};/g))enums[m[1]]=conditionalTokens(m[2]);
    for(const m of src.matchAll(/constexpr\s+char\s+const\s*\*\s+const\s+PROGMEM\s+(\w+)\s*\[\]\s*=\s*\{([\s\S]*?)\};/g))arrays[m[1]]=conditionalTokens(m[2]);
    for(const [name,enumName] of Object.entries(menuMap[system])){
      const options=arrays[name],members=enumName?enums[enumName]:null;
      if(!options)throw Error(system+' missing '+name);
      if(enumName&&(!members||members.length!==options.length))throw Error(system+' '+name+' enum mismatch');
      const entries=options.map((opt,i)=>{
        const symbol=opt.token.split('::').at(-1),label=labels[symbol]??english[symbol];
        if(!label)throw Error(system+' '+name+' unresolved '+opt.token);
        const member=members?members[i].token:null;
        if(members&&JSON.stringify(members[i].requires)!==JSON.stringify(opt.requires))throw Error(system+' '+name+' guard mismatch');
        const id=kebab(member||symbol);
        const back=symbol==='Back'||member==='Back';
        const dangerous=/write|flash|erase/i.test((member||'')+' '+symbol+' '+label);
        return {id,label,source_symbol:opt.token,enum_member:member,requires:opt.requires,kind:back?'navigation':'action',risk:dangerous?'write':'read',implemented:false};
      });
      if(new Set(entries.map(x=>x.id)).size!==entries.length)throw Error(system+' '+name+' duplicate ids');
      out.push({id:kebab(system)+'.'+kebab(name),system,source_array:name,source_enum:enumName,control:'actions',entries});
    }
  }
  if(db.outfile!=='nes-mappers'||!Array.isArray(db.records))throw Error('Unexpected mapper DB');
  const sizes={};
  for(const [kind,type] of Object.entries({prg:'uint16_t',chr:'uint16_t',ram:'uint8_t'})){
    const pattern=new RegExp('constexpr\\s+'+type+'\\s+const\\s+'+kind+'Sizes\\[\\]\\s*=\\s*\\{([^}]+)\\}','m');
    const m=coreSources.NES.match(pattern);
    if(!m)throw Error('Missing '+kind+'Sizes');
    sizes[kind]=m[1].replace(/\/\/.*$/gm,'').split(',').map(v=>v.trim()).filter(Boolean).map(Number);
    if(sizes[kind].some(v=>!Number.isSafeInteger(v)))throw Error('Invalid '+kind+' sizes');
  }
  const unique=new Set(),mappers=db.records.map(r=>{
    const id=r.mapper+':'+r.submapper;
    if(unique.has(id))throw Error('Duplicate mapper '+id);
    unique.add(id);
    const allowed={};
    for(const k of ['prg','chr','ram']){
      const lo=r[k+'lo'],hi=r[k+'hi'];
      if(!Number.isInteger(lo)||!Number.isInteger(hi)||lo>hi)throw Error('Invalid mapper limit '+id+' '+k);
      allowed[k+'_kib']=sizes[k].filter(v=>v>=lo&&v<=hi);
    }
    return {id,label:'Mapper '+r.mapper+(r.submapper?' / Submapper '+r.submapper:''),mapper:r.mapper,submapper:r.submapper,limits_kib:{prg:[r.prglo,r.prghi],chr:[r.chrlo,r.chrhi],ram:[r.ramlo,r.ramhi]},allowed_kib:allowed};
  });
  return {schema_version:1,source:{firmware:'oscartreader/oscr-firmware',language:'en',mapper_database:'oscartreader/oscr-databases/data/nes-mappers.json'},usage:{default_mode:'auto-detect',manual_override_optional:true,auto_detection:'Use the OSCR cartridge DB for cartridge identification',status:'schema-only; no menu action is executable yet'},menus:out,fields:{nes_mapper:{type:'select',default:'auto',values:[{id:'auto',label:'Automatic detection',mapper:null,submapper:null},...mappers]},nes_sizes:{source:'src/cores/NES.cpp',unit:'KiB',values:sizes}},warnings:['nes-mappers.json describes mapper capabilities, not cartridge identification.','Actions remain disabled until the kernel-backed CLI is implemented.','Write and flash actions are metadata for later implementation.']};
}
function main(argv){
  let root='third_party/oscr-firmware',dbpath=null,output=null;
  for(let i=0;i<argv.length;i++){
    if(!['--firmware-root','--database','--output'].includes(argv[i])||!argv[i+1])
      throw Error('Usage: node tools/oscr-export-web-menu.js [--firmware-root PATH] [--database PATH] [--output FILE]');
    const value=argv[++i],key=argv[i-1];
    if(key==='--firmware-root')root=value;
    if(key==='--database')dbpath=value;
    if(key==='--output')output=value;
  }
  dbpath??=path.join(root,'databases/data/nes-mappers.json');
  const read=x=>fs.readFileSync(x,'utf8'),coreSources={};
  for(const name of ['NES','MegaDrive','SNES'])coreSources[name]=read(path.join(root,'src/cores',name+'.cpp'));
  const result=extractCatalog(coreSources,read(path.join(root,'include/ui/l10n/en.h')),JSON.parse(read(dbpath)));
  const content=JSON.stringify(result,null,2)+'\n';
  if(output)fs.writeFileSync(output,content);else process.stdout.write(content);
}
if(require.main===module){try{main(process.argv.slice(2));}catch(e){console.error('OSCR web-menu export failed:',e.message);process.exitCode=1;}}
module.exports={extractCatalog,extractSymbols,conditionalTokens};
