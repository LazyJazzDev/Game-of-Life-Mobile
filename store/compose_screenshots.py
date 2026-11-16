"""Compose App Store screenshots: a caption over a rounded app capture.

Usage: compose_screenshots.py <raw-dir> <output-dir> [device ...] [--plan creative-plan.json]
With --plan, use <device>-<lang>-<scene>.png and retain each capture size.
Raw captures come from capture_screenshots.sh (<device>-<lang>-<n>-<name>.png).
Devices: iphone (6.9", 1320x2868), iphone-6.5 (1284x2778, from the iPhone
captures), ipad (13", 2064x2752), and harmony-phone (1080x1920).
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
import numpy as np, os, sys, json
from pathlib import Path
RAW, OUT = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
plan = None
if '--plan' in args:
    pos = args.index('--plan')
    plan = json.loads(Path(args[pos + 1]).read_text())
    del args[pos:pos + 2]
F='/System/Library/Fonts/Hiragino Sans GB.ttc'
text={'zh-Hans':[('running','简单规则，奇妙变化','点下播放，看小方格组成会动的图案'),
                 ('soup','加快时间，发现惊喜','从满屏小方格，到意想不到的新形状'),
                 ('lib','736 种图案，打开就玩','不必从零开始，选一个图案看看会发生什么'),
                 ('guns','找到喜欢的，一键开始','按分类浏览或搜索，轻点即可载入')],
      'en':[('running','Simple rules, surprising changes','Press play and watch little squares form moving patterns'),
            ('soup','Speed up. See what happens.','Watch a screen full of squares become unexpected shapes'),
            ('lib','736 patterns ready to explore','Pick a pattern and discover what happens next'),
            ('guns','Find a favorite. Tap to start.','Browse or search, then load a pattern with one tap')]}
dev={'iphone':dict(W=1320,H=2868,title=92,sub=48,ty=250,sy=365,top=500,bottom=120,r=72),
     'iphone-medium':dict(W=1206,H=2622,title=84,sub=44,ty=229,sy=334,top=457,bottom=110,r=66,raw='iphone'),
     'iphone-6.5':dict(W=1284,H=2778,title=90,sub=46,ty=242,sy=354,top=485,bottom=116,r=70,raw='iphone'),
     'ipad':dict(W=2064,H=2752,title=104,sub=54,ty=210,sy=330,top=450,bottom=110,r=44),
     'harmony-phone':dict(W=1080,H=1920,title=66,sub=34,ty=156,sy=238,top=335,bottom=80,r=48,
                          languages=['zh-Hans'], captures=['phone-gosper.jpeg','phone-random.jpeg','phone-library.jpeg','phone-guns-list.jpeg'])}
if plan:
    # Keep the original gradient, centered captions, frame and shadow.
    text = {lang: [(scene['id'], *scene[key]) for scene in plan['scenes']]
            for lang, key in [('zh-Hans', 'zh'), ('en', 'en')]}
    dev = {}
    for path in sorted(Path(RAW).glob('*-zh-Hans-discover.png')):
        name = path.name.removesuffix('-zh-Hans-discover.png')
        with Image.open(path) as shot: W, H = shot.size
        # Store-specific marketing canvas; the actual capture keeps its aspect ratio.
        if name in plan.get('canvas', {}):
            W, H = plan['canvas'][name]
            if W <= 0 or H <= 0:
                raise ValueError(f'Invalid canvas size for {name}')
        reference = dict(title=104,sub=54,ty=210,sy=330,top=450,bottom=110,r=44) if name == 'ipad' else dict(title=92,sub=48,ty=250,sy=365,top=500,bottom=120,r=72)
        baseW, baseH = (2064,2752) if name == 'ipad' else (1320,2868)
        dev[name] = dict(W=W,H=H,**{k:round(v*(H/baseH if k in ['ty','sy','top','bottom'] else min(W/baseW,H/baseH))) for k,v in reference.items()})
def fit(s,size,idx,maxw):
    while True:
        f=ImageFont.truetype(F,size,index=idx)
        if f.getlength(s)<=maxw or size<20: return f
        size-=2
def duo_outer_mask(width, height):
    # Apple Simulator iPhone Duo framebufferMask, 466 x 678 point outer display.
    # The hinge-side corners are small; the opposite corners are large continuous curves.
    points = [(10.25554, 678), (390.3654, 678)]
    curves = [
        (416.6652,678,426.2021,675.2616,435.8169,670.1196),
        (445.4318,664.9775,452.9775,657.4318,458.1196,647.8169),
        (463.2616,638.2021,466,628.6652,466,602.3654),
        (466,75.63461),
        (466,49.33481,463.2616,39.79787,458.1196,30.18305),
        (452.9775,20.56824,445.4318,13.02247,435.8169,7.880415),
        (426.2021,2.738356,416.6652,0,390.3654,0),
        (10.25554,0),
        (6.689466,0,5.396321,.3713024,4.092617,1.068531),
        (2.788913,1.765759,1.765759,2.788913,1.068531,4.092617),
        (.3713024,5.396321,0,6.689466,0,10.25554),
        (0,667.7445),
        (0,671.3105,.3713024,672.6037,1.068531,673.9074),
        (1.765759,675.2111,2.788913,676.2342,4.092617,676.9315),
        (5.396321,677.6287,6.689466,678,10.25554,678)]
    for curve in curves:
        if len(curve) == 2:
            points.append(curve)
            continue
        a = points[-1]; b, c, e = curve[:2], curve[2:4], curve[4:]
        for t in np.linspace(0, 1, 33)[1:]:
            points.append(tuple((1-t)**3*a[k]+3*(1-t)**2*t*b[k]+3*(1-t)*t*t*c[k]+t**3*e[k] for k in (0,1)))
    mask = Image.new('L', (width*2, height*2), 0)
    ImageDraw.Draw(mask).polygon([(x/466*width*2,(678-y)/678*height*2) for x,y in points], fill=255)
    return mask.resize((width,height), Image.Resampling.LANCZOS)
for d in args or (list(dev) if plan else ['iphone','iphone-6.5','ipad']):
    c=dev[d]; W,H=c['W'],c['H']
    y,x=np.mgrid[0:H,0:W]; t=((x/W)+(y/H))/2
    top=np.array([0.32,0.36,0.42]); bot=np.array([0.16,0.18,0.21])
    bg=((top*(1-t[...,None])+bot*t[...,None])*255).astype(np.uint8)
    for lang,items in text.items():
        if lang not in c.get('languages',text): continue
        os.makedirs(f'{OUT}/{d}/{lang}',exist_ok=True)
        for i,(name,title,sub) in enumerate(items,1):
            canvas=Image.fromarray(bg).convert('RGBA')
            filename=c['captures'][i-1] if 'captures' in c else f'{c.get("raw",d)}-{lang}-{i}-{name}.png'
            if plan: filename=f'{d}-{lang}-{name}.png'
            shot=Image.open(f'{RAW}/{filename}').convert('RGB')
            sh=H-c['top']-c['bottom']; sw=round(shot.width*sh/shot.height)
            if plan and sw > W * .90:
                sw=round(W*.90); sh=round(shot.height*sw/shot.width)
            shot=shot.resize((sw,sh),Image.LANCZOS)
            x0=(W-sw)//2; y0=c['top']; r=c['r']
            mask=Image.new('L',(sw,sh),0); ImageDraw.Draw(mask).rounded_rectangle((0,0,sw-1,sh-1),radius=r,fill=255)
            shadow=Image.new('RGBA',(W,H),(0,0,0,0)); ImageDraw.Draw(shadow).rounded_rectangle((x0,y0+20,x0+sw,y0+sh+20),radius=r,fill=(0,0,0,140))
            if d == 'iphone-duo-outer':
                mask = duo_outer_mask(sw, sh)
                shadow = Image.new('RGBA',(W,H),(0,0,0,0))
                shadow.paste((0,0,0,140),(x0,y0+20,x0+sw,y0+sh+20),mask)
            canvas=Image.alpha_composite(canvas,shadow.filter(ImageFilter.GaussianBlur(30)))
            canvas.paste(shot,(x0,y0),mask)
            dr=ImageDraw.Draw(canvas)
            if d == 'iphone-duo-outer':
                # Pad before erosion so the straight screen edges also receive a border.
                padded = Image.new('L',(sw+6,sh+6),0); padded.paste(mask,(3,3))
                inner = padded.filter(ImageFilter.MinFilter(7)).crop((3,3,sw+3,sh+3))
                border = ImageChops.subtract(mask,inner)
                canvas.paste((255,255,255,40),(x0,y0,x0+sw,y0+sh),border)
            else:
                dr.rounded_rectangle((x0,y0,x0+sw-1,y0+sh-1),radius=r,outline=(255,255,255,40),width=3)
            dr.text((W/2,c['ty']),title,font=fit(title,c['title'],1,W*0.9),fill=(240,244,250),anchor='mm')
            dr.text((W/2,c['sy']),sub,font=fit(sub,c['sub'],0,W*0.9),fill=(190,198,210),anchor='mm')
            canvas.convert('RGB').save(f'{OUT}/{d}/{lang}/{i}-{name}.png',optimize=True)
print('done')
