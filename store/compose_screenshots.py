"""Compose App Store screenshots: a caption over a rounded app capture.

Usage: compose_screenshots.py <raw-dir> <output-dir> [device ...]
Raw captures come from capture_screenshots.sh (<device>-<lang>-<n>-<name>.png).
Devices: iphone (6.9", 1320x2868), iphone-6.5 (1284x2778, from the iPhone
captures) and ipad (13", 2064x2752).
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np, os, sys
RAW, OUT = sys.argv[1], sys.argv[2]
F='/System/Library/Fonts/Hiragino Sans GB.ttc'
text={'zh-Hans':[('running','简单规则，复杂世界','Gosper 滑翔机枪正在不断发射滑翔机'),
                 ('soup','随机生成，闪电演化','每秒 60 代，看混沌沉淀成秩序'),
                 ('lib','内置 736 种著名图案','静物、振荡器、飞船、枪……分类整理'),
                 ('guns','分类浏览，一键载入','支持按名称搜索，列表标注每个图案的尺寸')],
      'en':[('running','Simple rules, complex worlds','A Gosper glider gun fires an endless stream of gliders'),
            ('soup','Randomize, then go lightning fast','60 generations per second: watch chaos settle into order'),
            ('lib','736 famous patterns built in','Still lifes, oscillators, spaceships, guns and more'),
            ('guns','Browse by category, load in one tap','Search by name; every pattern lists its size')]}
dev={'iphone':dict(W=1320,H=2868,title=92,sub=48,ty=250,sy=365,top=500,bottom=120,r=72),
     'iphone-6.5':dict(W=1284,H=2778,title=90,sub=46,ty=242,sy=354,top=485,bottom=116,r=70,raw='iphone'),
     'ipad':dict(W=2064,H=2752,title=104,sub=54,ty=210,sy=330,top=450,bottom=110,r=44)}
def fit(s,size,idx,maxw):
    while True:
        f=ImageFont.truetype(F,size,index=idx)
        if f.getlength(s)<=maxw or size<20: return f
        size-=2
for d in sys.argv[3:] or dev:
    c=dev[d]; W,H=c['W'],c['H']
    y,x=np.mgrid[0:H,0:W]; t=((x/W)+(y/H))/2
    top=np.array([0.32,0.36,0.42]); bot=np.array([0.16,0.18,0.21])
    bg=((top*(1-t[...,None])+bot*t[...,None])*255).astype(np.uint8)
    for lang,items in text.items():
        os.makedirs(f'{OUT}/{d}/{lang}',exist_ok=True)
        for i,(name,title,sub) in enumerate(items,1):
            canvas=Image.fromarray(bg).convert('RGBA')
            shot=Image.open(f'{RAW}/{c.get("raw",d)}-{lang}-{i}-{name}.png').convert('RGB')
            sh=H-c['top']-c['bottom']; sw=round(shot.width*sh/shot.height)
            shot=shot.resize((sw,sh),Image.LANCZOS)
            x0=(W-sw)//2; y0=c['top']; r=c['r']
            mask=Image.new('L',(sw,sh),0); ImageDraw.Draw(mask).rounded_rectangle((0,0,sw-1,sh-1),radius=r,fill=255)
            shadow=Image.new('RGBA',(W,H),(0,0,0,0)); ImageDraw.Draw(shadow).rounded_rectangle((x0,y0+20,x0+sw,y0+sh+20),radius=r,fill=(0,0,0,140))
            canvas=Image.alpha_composite(canvas,shadow.filter(ImageFilter.GaussianBlur(30)))
            canvas.paste(shot,(x0,y0),mask)
            dr=ImageDraw.Draw(canvas)
            dr.rounded_rectangle((x0,y0,x0+sw-1,y0+sh-1),radius=r,outline=(255,255,255,40),width=3)
            dr.text((W/2,c['ty']),title,font=fit(title,c['title'],1,W*0.9),fill=(240,244,250),anchor='mm')
            dr.text((W/2,c['sy']),sub,font=fit(sub,c['sub'],0,W*0.9),fill=(190,198,210),anchor='mm')
            canvas.convert('RGB').save(f'{OUT}/{d}/{lang}/{i}-{name}.png',optimize=True)
print('done')
