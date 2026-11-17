// Generic unbounded B3/S23 evolution; no patterns or construction recipes.
const life = {
  parseRLE(text) {
    if (typeof text !== 'string' || text.length > 4*1024*1024) throw Error('Expected RLE text (up to 4 MiB)');
    const lines=text.split(/\r?\n/).map(s=>s.trim()).filter(s=>s && !s.startsWith('#'));
    if (lines[0] && /^x\s*=/.test(lines[0])) {
      const header=lines.shift();
      const rule=header.match(/rule\s*=\s*([^,]+)/i);
      if(rule && !/^(B3\/S23|23\/3)$/i.test(rule[1].trim())) throw Error('Only B3/S23 RLE is supported');
    }
    const body=lines.join('').replace(/\s/g,'');
    let x=0,y=0,num='',ended=false;const cells=[];
    for(let i=0;i<body.length;i++) {
      const c=body[i];
      if(c>='0' && c<='9'){num+=c;if(num.length>8)throw Error('RLE run exceeds limit');continue;}
      const n=num ? Number(num) : 1;
      if(!Number.isSafeInteger(n)||n<1||n>2097153)throw Error('Invalid RLE run');
      if(c==='!') {if(num||i!==body.length-1)throw Error('Invalid RLE terminator');ended=true;break;}
      num='';
      if(c==='$'){y+=n;x=0;if(y>1048576)throw Error('RLE coordinate exceeds limit');}
      else if(c==='b'||c==='o') {
        if(x+n>1048577)throw Error('RLE coordinate exceeds limit');
        if(c==='o') {
          if(cells.length+n>4194304)throw Error('RLE population exceeds limit');
          for(let j=0;j<n;j++)cells.push([x+j,y]);
        }
        x+=n;
      } else throw Error('Invalid RLE symbol: '+c);
    }
    if(!ended)throw Error('RLE terminator ! is required');
    return cells;
  },
  encodeRLE(cells) {
    if(!Array.isArray(cells)||cells.length>4194304)throw Error('Expected cells:[[x,y],...]');
    const rows=new Map();let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
    for(const p of cells) {
      if(!Array.isArray(p)||p.length!==2||!p.every(n=>Number.isSafeInteger(n)&&Math.abs(n)<=1048576))throw Error('Invalid cell coordinate');
      const [x,y]=p;
      if(!rows.has(y))rows.set(y,new Set());rows.get(y).add(x);
      minX=Math.min(minX,x);minY=Math.min(minY,y);maxX=Math.max(maxX,x);maxY=Math.max(maxY,y);
    }
    if(!cells.length)return {rle:'x = 0, y = 0, rule = B3/S23\n!',origin:[0,0]};
    if(maxX-minX>1048576||maxY-minY>1048576)throw Error('RLE span exceeds limit');
    const tokens=[];let prevY=minY;
    const emit=(n,c)=>{if(n>0)tokens.push((n===1?'':String(n))+c);};
    for(const y of [...rows.keys()].sort((a,b)=>a-b)) {
      emit(y-prevY,'$');prevY=y;let nextX=minX;
      const xs=[...rows.get(y)].sort((a,b)=>a-b);
      for(let i=0;i<xs.length;) {
        const start=xs[i];emit(start-nextX,'b');let end=start;i++;
        while(i<xs.length&&xs[i]===end+1)end=xs[i++];
        emit(end-start+1,'o');nextX=end+1;
      }
    }
    tokens.push('!');let body='',line='';
    for(const token of tokens){if(line.length+token.length>70){body+=line+'\n';line='';}line+=token;}
    const rle=`x = ${maxX-minX+1}, y = ${maxY-minY+1}, rule = B3/S23\n${body+line}`;
    if(rle.length>4*1024*1024)throw Error('RLE output exceeds limit');
    return {rle,origin:[minX,minY]};
  },
  evolve(cells, generations) {
    if (!Array.isArray(cells) || cells.length > 4194304 || !Number.isSafeInteger(generations) || generations < 0 || generations > 1048576)
      throw Error('Expected cells:[[x,y],...] and 0..1048576 generations');
    if (!cells.length) return [];
    const xs=new Int32Array(cells.length), ys=new Int32Array(cells.length);
    cells.forEach((p,i)=>{
      if (!Array.isArray(p) || p.length!==2 || !Number.isSafeInteger(p[0]) || !Number.isSafeInteger(p[1]) || Math.abs(p[0])>1048576 || Math.abs(p[1])>1048576)
        throw Error('Invalid cell coordinate');
      xs[i]=p[0];ys[i]=p[1];
    });
    const universe=new LifeUniverse();universe.setup_field(xs,ys);
    while(generations>0) {
      const exponent=Math.floor(Math.log2(generations));
      universe.set_step(exponent);universe.next_generation(true);
      generations-=2**exponent;
    }
    if(universe.root.population>4194304)throw Error('Evolution result exceeds population limit');
    const result=[];
    function visit(node,x,y) {
      if(!node.population)return;
      if(node.level===0){result.push([x,y]);return;}
      const half=2**(node.level-1);
      visit(node.nw,x,y);visit(node.ne,x+half,y);visit(node.sw,x,y+half);visit(node.se,x+half,y+half);
    }
    const offset=2**(universe.root.level-1);visit(universe.root,-offset,-offset);
    return result;
  }
};
