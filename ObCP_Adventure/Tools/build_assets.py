#!/usr/bin/env python3
"""Compile les sprites, les tuiles et la police du jeu en données C constantes.

    python Tools/build_assets.py

Entrée  : Assets/pixel_art.json (chaque pixel est un chiffre hexadécimal, indice
          de couleur 0 à 15 ; la palette est dans Core/Src/graphics/oa_render.c).
Sorties : Core/Inc/oa_assets.h, Core/Src/graphics/oa_assets.c et
          Assets/assets_manifest.json (empreinte SHA-256 de la source).
Les formes ont été créées pour ObCP Adventure ; aucune image de jeu tiers.
La sortie 4 bits par pixel est déterministe et n'alloue rien sur la carte.
N'utilise que la bibliothèque standard de Python 3.
SPDX-License-Identifier: MIT
"""
from pathlib import Path
import json, hashlib
ROOT=Path(__file__).resolve().parents[1]
def make_sources():
    art={}
    def canvas(w,h):return [[0]*w for _ in range(h)]
    def rect(im,x,y,w,h,c):
        for Y in range(max(0,y),min(len(im),y+h)):
            for X in range(max(0,x),min(len(im[0]),x+w)):im[Y][X]=c
    for f in range(6):
        a=canvas(16,24)
        rect(a,7,0,2,3,7);rect(a,3,3,11,10,1);rect(a,4,4,9,8,10)
        rect(a,6,6,7,4,1);rect(a,9,7,3,2,2);rect(a,4,13,8,7,4)
        rect(a,5,14,5,2,2);rect(a,1,13,3,6,6);rect(a,12,14,3,4,6)
        if f==4:
            rect(a,4,20,4,2,1);rect(a,10,19,4,3,1)
        else:
            rect(a,4,20,3,3-(f%2),1);rect(a,9,20,3,2+(f%2),1)
            rect(a,2+(f%2)*2,22,5,2,11);rect(a,9,22-(f%2),5,2,11)
        art[f'robot_{f}']=a
    for name in ['walker','hopper','flyer','turret']:
        for f in range(2):
            a=canvas(16,16)
            if name=='walker':
                rect(a,2,5,12,8,1);rect(a,3,5,10,6,9);rect(a,10,7,3,2,2)
                for x in [1,6,11]:rect(a,x,12+(f if x==6 else 1-f),3,2,11)
            elif name=='hopper':
                rect(a,4,2,8,11,5);rect(a,6,0,3,5,4);rect(a,9,5,3,3,2);rect(a,10,6,1,1,1)
                rect(a,1,13-f*2,6,3,1);rect(a,10,13-f*2,6,3,1)
            elif name=='flyer':
                for x,y in [(0,1+f*5),(10,1+f*5)]:rect(a,x,y,6,6,12)
                rect(a,6,4,4,10,9);rect(a,6,5,4,2,7)
            else:
                rect(a,3,9,10,7,11);rect(a,2,5,12,6,1);rect(a,4,6,9,3,8 if f else 4);rect(a,0,7,6,3,11)
            art[f'{name}_{f}']=a
    for f in range(2):
        a=canvas(32,32)
        for y in range(32):
            for x in range(32):
                d=(x-15)**2+(y-17)**2
                if d<220:a[y][x]=6 if d>150 else 4
        for x in [2,11,20,27]:rect(a,x,0,3,9,5);rect(a,x,27,3,5,1)
        rect(a,5,10,23,10,1);rect(a,7,12,19,6,8 if f else 10)
        rect(a,10,13,4,3,2);rect(a,20,13,4,3,2);rect(a,11,23,11,3,7)
        art[f'boss_{f}']=a
    for tile in range(8):
        a=canvas(16,16)
        for y in range(16):
            for x in range(16):
                if tile==1:a[y][x]=5 if y<3 else (6 if (x*7+y*3)%17<3 else 1)
                elif tile==2:a[y][x]=11 if y%8==0 or (x+(8 if y>8 else 0))%16==0 else 6
                elif tile==3:a[y][x]=11 if y<4 else (4 if y<7 and x%4<2 else 0)
                elif tile==4:a[y][x]=8 if y>=15-abs(x%8-4)*3 else 0
                elif tile==5:a[y][x]=7 if x in [0,15] or y in [0,15] else 6
                elif tile==6:a[y][x]=6 if (x+y)%7 else 1
                elif tile==7:a[y][x]=10 if y>2 or x%7<3 else 0
        if tile==5:rect(a,7,4,2,6,2);rect(a,7,12,2,2,2)
        art[f'tile_{tile}']=a
    # Police 5x7 dessinée sous forme de lignes ; les accents sont translittérés
    # par l'interface embarquée pour conserver un alphabet compact.
    font={
      'A':['01110','10001','10001','11111','10001','10001','10001'],
      'B':['11110','10001','10001','11110','10001','10001','11110'],
      'C':['01111','10000','10000','10000','10000','10000','01111'],
      'D':['11110','10001','10001','10001','10001','10001','11110'],
      'E':['11111','10000','10000','11110','10000','10000','11111'],
      'F':['11111','10000','10000','11110','10000','10000','10000'],
      'G':['01111','10000','10000','10111','10001','10001','01111'],
      'H':['10001','10001','10001','11111','10001','10001','10001'],
      'I':['111','010','010','010','010','010','111'],
      'J':['00111','00010','00010','00010','00010','10010','01100'],
      'K':['10001','10010','10100','11000','10100','10010','10001'],
      'L':['10000','10000','10000','10000','10000','10000','11111'],
      'M':['10001','11011','10101','10101','10001','10001','10001'],
      'N':['10001','11001','10101','10011','10001','10001','10001'],
      'O':['01110','10001','10001','10001','10001','10001','01110'],
      'P':['11110','10001','10001','11110','10000','10000','10000'],
      'Q':['01110','10001','10001','10001','10101','10010','01101'],
      'R':['11110','10001','10001','11110','10100','10010','10001'],
      'S':['01111','10000','10000','01110','00001','00001','11110'],
      'T':['11111','00100','00100','00100','00100','00100','00100'],
      'U':['10001','10001','10001','10001','10001','10001','01110'],
      'V':['10001','10001','10001','10001','10001','01010','00100'],
      'W':['10001','10001','10001','10101','10101','11011','10001'],
      'X':['10001','10001','01010','00100','01010','10001','10001'],
      'Y':['10001','10001','01010','00100','00100','00100','00100'],
      'Z':['11111','00001','00010','00100','01000','10000','11111'],
      '0':['01110','10001','10011','10101','11001','10001','01110'],
      '1':['00100','01100','00100','00100','00100','00100','01110'],
      '2':['01110','10001','00001','00010','00100','01000','11111'],
      '3':['11110','00001','00001','01110','00001','00001','11110'],
      '4':['00010','00110','01010','10010','11111','00010','00010'],
      '5':['11111','10000','10000','11110','00001','00001','11110'],
      '6':['01110','10000','10000','11110','10001','10001','01110'],
      '7':['11111','00001','00010','00100','01000','01000','01000'],
      '8':['01110','10001','10001','01110','10001','10001','01110'],
      '9':['01110','10001','10001','01111','00001','00001','01110'],
      ':':['00000','00100','00100','00000','00100','00100','00000'],
      '-':['00000','00000','00000','11111','00000','00000','00000'],
      '/':['00001','00010','00010','00100','01000','01000','10000'],
      '.':['00000','00000','00000','00000','00000','00100','00100'],
      '+':['00000','00100','00100','11111','00100','00100','00000'],
      '?':['01110','10001','00001','00010','00100','00000','00100'],
      '<':['00010','00100','01000','10000','01000','00100','00010'],
      '>':['01000','00100','00010','00001','00010','00100','01000'],
      '=':['00000','00000','11111','00000','11111','00000','00000']}
    return {'sprites':{name:[''.join(format(c,'x') for c in row) for row in im] for name,im in art.items()},'font':font}
def build():
    src=ROOT/'Assets/pixel_art.json'
    if not src.exists():src.write_text(json.dumps(make_sources(),indent=2),encoding='utf-8')
    data=json.loads(src.read_text(encoding='utf-8'));names=list(data['sprites']);out=['/* Generated, original ObCP pixel art. SPDX-License-Identifier: MIT */','#include "oa_assets.h"']
    h=['/** @file oa_assets.h @brief Ressources indexées 4 bpp originales. */','#ifndef OA_ASSETS_H','#define OA_ASSETS_H','#include <stdint.h>','typedef struct {uint8_t w,h;const uint8_t *data;} OaSprite;']
    for i,name in enumerate(names):
        rows=data['sprites'][name];w=len(rows[0]);assert w%2==0 and all(len(r)==w for r in rows)
        pixels=[int(c,16) for r in rows for c in r];packed=[pixels[k]*16+pixels[k+1] for k in range(0,len(pixels),2)]
        h+=['#define OA_ASSET_'+name.upper()+' '+str(i)]
        out+=['static const uint8_t data_'+name+'[] = {'+','.join(map(str,packed))+'};']
    out+=['const OaSprite oa_sprites[] = {']+['{%d,%d,data_%s},'%(len(data['sprites'][n][0]),len(data['sprites'][n]),n) for n in names]+['};']
    font=[]
    for code in range(32,96):
        rows=data['font'].get(chr(code),['00000']*7);font.extend(int(r.center(5,'0'),2) for r in rows)
    out+=['const uint8_t oa_font[64][7] = {']+['{'+','.join(map(str,font[i:i+7]))+'},' for i in range(0,len(font),7)]+['};','']
    h+=['extern const OaSprite oa_sprites[];','extern const uint8_t oa_font[64][7];','#endif','']
    (ROOT/'Core/Inc/oa_assets.h').write_text('\n'.join(h),encoding='utf-8')
    (ROOT/'Core/Src/graphics/oa_assets.c').write_text('\n'.join(out),encoding='utf-8')
    (ROOT/'Assets/assets_manifest.json').write_text(json.dumps({'license':'MIT','origin':'Original algorithmic pixel art for ObCP Adventure','source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'sprite_count':len(names)},indent=2),encoding='utf-8')
    print(len(names),'sprites et tuiles, police de 64 caractères : Core/Src/graphics/oa_assets.c')
if __name__=='__main__':build()
