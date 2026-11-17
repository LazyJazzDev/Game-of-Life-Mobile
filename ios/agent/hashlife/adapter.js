// Generic unbounded B3/S23 evolution; no patterns or construction recipes.
const life = {
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
