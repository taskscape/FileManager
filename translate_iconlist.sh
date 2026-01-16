#!/bin/bash
# Translation script for iconlist.cpp Czech comments

cd /c/Projects/FileManager

# Create backup
cp src/iconlist.cpp src/iconlist.cpp.backup

# Line 12-13: Number of items in row
sed -i '12s|// udava pocet polozek v jedne radce; po vzoru W2K davame 4;|// specifies the number of items in one row; following W2K we use 4;|' src/iconlist.cpp
sed -i '13s|// asi pro vetsi rychlost provadenych operaci?|// probably for greater speed of operations?|' src/iconlist.cpp

# Line 16-18: Icon types
sed -i '16s|// bezna ikona, kterou lze vykreslit pres BitBlt|// normal icon, which can be drawn via BitBlt|' src/iconlist.cpp
sed -i '17s|// ikona obsahujici oblasti, ktere je treba XORovat|// icon containing areas that need to be XORed|' src/iconlist.cpp
sed -i '18s|// ikona obsahujici alfa kanal|// icon containing alpha channel|' src/iconlist.cpp

# Line 123: If old bitmap exists
sed -i '123s|// pokud existuje stara bitmapa, zrusime ji|// if old bitmap exists, destroy it|' src/iconlist.cpp

# Line 155: Without alpha channel
sed -i '155s|// bez alfa kanalu|// without alpha channel|' src/iconlist.cpp

# Line 160: Helper DC
sed -i '160s|// pomocne DC, abychom mohli vytvorit kompatibilni bitmapu|// helper DC, so we can create a compatible bitmap|' src/iconlist.cpp

# Line 187: If cache/mask bitmap doesn't exist
sed -i '187s|// pokud jeste neexistuje cache/mask bitmapa nebo je mala, vytvorime ji|// if the cache/mask bitmap does not yet exist or is too small, create it|' src/iconlist.cpp

# Line 103-104: Warning about BITMAPV4HEADER
sed -i '103s|// !!! Pozor, pri pouzivani BITMAPV4HEADER u Petra padal Salamander v ICM32.DLL,|// !!! Warning, when using BITMAPV4HEADER on Petr'\''s system Salamander crashed in ICM32.DLL,|' src/iconlist.cpp
sed -i '104s|// stary BITMAPINFOHEADER zjevne staci|// old BITMAPINFOHEADER is apparently sufficient|' src/iconlist.cpp

# Line 196-197: Same warning
sed -i '196s|// !!! Pozor, pri pouzivani BITMAPV4HEADER u Petra padal Salamander v ICM32.DLL,|// !!! Warning, when using BITMAPV4HEADER on Petr'\''s system Salamander crashed in ICM32.DLL,|' src/iconlist.cpp
sed -i '197s|// stary BITMAPINFOHEADER zjevne staci|// old BITMAPINFOHEADER is apparently sufficient|' src/iconlist.cpp

# Line 241: Extract geometry of image list
sed -i '241s|// vytahneme geometrii image listu|// extract the geometry of the image list|' src/iconlist.cpp

# Line 267: Optimization note
sed -i '267s|// j.r. tady by se dalo optimalizovat: misto predavani po ikonach predat pres ImageList_Draw|// j.r. this could be optimized: instead of passing icons one by one, pass them via ImageList_Draw|' src/iconlist.cpp

# Line 290-293: Alpha channel comment
sed -i '290s|// Pokud mam pod W2K desktop 32bpp, dostavam XP ikonky s alfa kanalem neorezane,|// If I have a 32bpp desktop under W2K, I get XP icons with the alpha channel unclipped,|' src/iconlist.cpp
sed -i '291s|// tedy vcetne alfa kanalu. Pokud prepnu desktop na 16bpp, je alfa kanal orezany (nulovany).|// i.e. including the alpha channel. If I switch the desktop to 16bpp, the alpha channel is clipped (zeroed).|' src/iconlist.cpp
sed -i '292s|// Pokud tedy ApplyMaskToImage dostane ikonku vcetne alfa kanalu, vrati typ|// So if ApplyMaskToImage receives an icon including the alpha channel, it will return type|' src/iconlist.cpp
sed -i '293s|// IL_TYPE_ALPHA a Salamander bude korektne zobrazovat tyto ikony i pod OS < XP|// IL_TYPE_ALPHA and Salamander will correctly display these icons even under OS < XP|' src/iconlist.cpp

# Line 299: Coordinates in points
sed -i '299s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 303: Examine whole icon
sed -i '303s|// prohledneme celou ikonku a urcime o jaky typ se jedna (NORMAL, XOR, ALPHA)|// examine the whole icon and determine what type it is (NORMAL, XOR, ALPHA)|' src/iconlist.cpp

# Line 325-326: XOR candidate note
sed -i '325s|// to ze je ikonka kandidat na XOR jeste neznamena,|// the fact that the icon is a candidate for XOR does not yet mean,|' src/iconlist.cpp
sed -i '326s|// ze nebude ALPHA, takze nemuzeme vypadnout|// that it won'\''t be ALPHA, so we can'\''t exit|' src/iconlist.cpp

# Line 339: Prepare background color
sed -i '339s|// pripravime si barvu pozadi ve spravnem formatu|// prepare the background color in the correct format|' src/iconlist.cpp

# Line 341: Transfer transparent areas
sed -i '341s|// pruhledne oblasti masky preneseme do alfa kanalu|// transfer transparent areas of the mask to the alpha channel|' src/iconlist.cpp

# Line 353-354: Fully transparent area
sed -i '353s|// zcela pruhledna oblast|// fully transparent area|' src/iconlist.cpp
sed -i '354s|// alfa kanal je v nejvyssim bytu, nastavime na 00, zbytek bude barva pozadi|// alpha channel is in the highest byte, set to 00, the rest will be background color|' src/iconlist.cpp

# Line 359: Set as non-transparent
sed -i '359s|// oblast nastavime jako nepruhlednou|// set the area as non-transparent|' src/iconlist.cpp

# Line 377: Non-transparent area
sed -i '377s|// nepruhledna oblast|// non-transparent area|' src/iconlist.cpp

# Line 461: Parameter check
sed -i '461s|// kontrola parametru|// parameter check|' src/iconlist.cpp

# Line 468-470: Resize icon
sed -i '468s|// pokud je treba, provedeme resize ikonky|// if necessary, resize the icon|' src/iconlist.cpp
sed -i '469s|// Honza: pod W10 mi zacalo volani CopyImage padat v debug x64 verzi, pokud byl povolen LR_COPYFROMRESOURCE flag|// Honza: under W10, CopyImage started crashing in debug x64 version if LR_COPYFROMRESOURCE flag was enabled|' src/iconlist.cpp
sed -i '470s|// FIXME: provest audit, zda je tento downscale jeste potreba, kdyz SalLoadIcon() nove vola LoadIconWithScaleDown()|// FIXME: audit whether this downscale is still needed when SalLoadIcon() now calls LoadIconWithScaleDown()|' src/iconlist.cpp

# Line 473: Extract MASK and COLOR bitmaps
sed -i '473s|// vytahneme z handlu ikony jeji MASK a COLOR bitmapy|// extract the MASK and COLOR bitmaps from the icon handle|' src/iconlist.cpp

# Line 488: Black and white icon
sed -i '488s|// pokud se jedna o b&w ikonu, mela by mit sudou vysku|// if this is a b&w icon, it should have even height|' src/iconlist.cpp

# Line 494: Icon should have same dimensions
sed -i '494s|// ikonka by mela mit stejne rozmery, jako ma nase polozka|// the icon should have the same dimensions as our item|' src/iconlist.cpp

# Line 500: Need sufficient space for mask
sed -i '500s|// potrebujeme dostatecny prostor pro masku|// need sufficient space for the mask|' src/iconlist.cpp

# Line 504: Helper DC for bitblt
sed -i '504s|// pomocne dc pro bitblt|// helper DC for bitblt|' src/iconlist.cpp

# Line 511: Color part of icon
sed -i '511s|// ii.hbmColor -> HImage (pokud je hbmColor==NULL, lezi XOR cast ve spodni polovine hbmMask|// ii.hbmColor -> HImage (if hbmColor==NULL, the XOR part is in the lower half of hbmMask|' src/iconlist.cpp

# Line 517: According to MSDN
sed -i '517s|// podle MSDN je treba zavolat nez zacneme pristupovat na raw data|// according to MSDN it is necessary to call before we start accessing raw data|' src/iconlist.cpp

# Line 537: If we changed size
sed -i '537s|// pokud jsme menili velikost, musime sestrelit docasnou ikonku|// if we changed size, we must destroy the temporary icon|' src/iconlist.cpp

# Line 564: Parameter check
sed -i '564s|// kontrola parametru|// parameter check|' src/iconlist.cpp

# Line 571: Create B&W mask
sed -i '571s|// vytvorime B&W masku + barevnou bitmapu dle obrazovky|// create B&W mask + colored bitmap according to screen|' src/iconlist.cpp

# Line 584: Prepare COLOR part
sed -i '584s|// do HTmpImage pripravime COLOR cast ikonky|// prepare the COLOR part of the icon in HTmpImage|' src/iconlist.cpp

# Line 596: On XP and newer
sed -i '596s|// az na XP a novejsich systemech vracime ikony s alphou|// only on XP and newer systems we return icons with alpha|' src/iconlist.cpp

# Line 601-603: Transparent area note
sed -i '601s|// v pruhledne oblasti je barva pozadi image listu, ale my tam|// in the transparent area there is the background color of the image list, but we|' src/iconlist.cpp
sed -i '602s|// musime dodat cernou barvu, aby fungoval XOR mechanismus kresleni|// must provide black color for the XOR drawing mechanism to work|' src/iconlist.cpp

# Line 615: Transfer HTmpImage
sed -i '615s|// preneseme HTmpImage do hColor|// transfer HTmpImage to hColor|' src/iconlist.cpp

# Line 623: Prepare MASK part
sed -i '623s|// do HTmpImage pripravime MASK cast ikonky|// prepare the MASK part of the icon in HTmpImage|' src/iconlist.cpp

# Line 641: Transfer HTmpImage
sed -i '641s|// preneseme HTmpImage do hMask|// transfer HTmpImage to hMask|' src/iconlist.cpp

# Line 650: Clean up
sed -i '650s|// zameteme|// clean up|' src/iconlist.cpp

# Line 656: Create icon from hColor + hMask
sed -i '656s|// z hColor + hMask vytvorime ikonku|// create icon from hColor + hMask|' src/iconlist.cpp

# Line 664: Must not be in handles
sed -i '664s|// nesmi byt v handles, vyvazi ikonku ven ze Salamandera|// must not be in handles, exports the icon out of Salamander|' src/iconlist.cpp

# Line 674: Parameter check
sed -i '674s|// kontrola parametru|// parameter check|' src/iconlist.cpp

# Line 681: Mask is used in drag&drop
sed -i '681s|// maska se pouziva pri drag&dropu, napriklad u shared adesaru, viz StateImageList_Draw()|// mask is used in drag&drop, for example for shared directories, see StateImageList_Draw()|' src/iconlist.cpp

# Line 715: Coordinates in points
sed -i '715s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 721-724: DrawMask notes
sed -i '721s|// sosneme data z obrazovky do HTmpImage|// save data from screen to HTmpImage|' src/iconlist.cpp
sed -i '722s|// nase DrawMask pouze nastavuje cerne body v miste masky, abychom mohli snadno mergovat s overlayem|// our DrawMask only sets black points at the mask location, so we can easily merge with overlay|' src/iconlist.cpp
sed -i '723s|// pri volani z StateImageList_Draw(); pokud bude casem treba zobrazovat dalsi overlay, bude to chtit|// when called from StateImageList_Draw(); if it becomes necessary to display other overlays in the future, it will require|' src/iconlist.cpp
sed -i '724s|// udelat v StateImageList_Draw() merger na zaklade boolovskych bitblt operaci a tato metoda by pak|// making a merger in StateImageList_Draw() based on boolean bitblt operations and this method would then|' src/iconlist.cpp

# Line 725-726: More DrawMask notes
sed -i '725s|// mohla nastavovat take fgColor; odpadla by podminka \*\*\* dole|// also be able to set fgColor; the condition *** below would be eliminated|' src/iconlist.cpp

# Line 730: Draw to HTmpImage
sed -i '730s|// budeme kreslit do HTmpImage|// we will draw to HTmpImage|' src/iconlist.cpp

# Line 742: See comment above
sed -i '742s|// \*\*\* viz komentar nahore|// *** see comment above|' src/iconlist.cpp

# Line 750: Draw HTmpImage to screen
sed -i '750s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 762: Coordinates in points
sed -i '762s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 766: Draw to HTmpImage
sed -i '766s|// budeme kreslit do HTmpImage|// we will draw to HTmpImage|' src/iconlist.cpp

# Line 792: Draw HTmpImage to screen
sed -i '792s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 804: Coordinates in points
sed -i '804s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 808: Transfer data to HMask
sed -i '808s|// v prvni fazi preneseme data do HMask|// in the first phase transfer data to HMask|' src/iconlist.cpp

# Line 847: Draw HTmpImage to screen
sed -i '847s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 859: Coordinates in points
sed -i '859s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 863-864: Transfer from target DC
sed -i '863s|// v prvni fazi preneseme z ciloveho DC data do HMask|// in the first phase transfer data from target DC to HMask|' src/iconlist.cpp
sed -i '866s|// podle MSDN je treba zavolat nez zacneme pristupovat na raw data|// according to MSDN it is necessary to call before we start accessing raw data|' src/iconlist.cpp

# Line 908: Draw HTmpImage to screen
sed -i '908s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 920: Coordinates in points
sed -i '920s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 924-925: Transfer from target DC
sed -i '924s|// v prvni fazi preneseme z ciloveho DC data do HMask|// in the first phase transfer data from target DC to HMask|' src/iconlist.cpp
sed -i '927s|// podle MSDN je treba zavolat nez zacneme pristupovat na raw data|// according to MSDN it is necessary to call before we start accessing raw data|' src/iconlist.cpp

# Line 942: All channels carry the same value
sed -i '942s|// vsechny kanaly nesou stejnou hodnotu, kterou mame povazovat za alpha kanal|// all channels carry the same value, which we should consider as alpha channel|' src/iconlist.cpp

# Line 959: Draw HTmpImage to screen
sed -i '959s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 971: Coordinates in points
sed -i '971s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 975: Is it XOR type variant?
sed -i '975s|// jde o variantu, kde je treba XORovat?|// is it a variant where XORing is needed?|' src/iconlist.cpp

# Line 978: Prepare raw data
sed -i '978s|// v prvni pripravime raw data pro HTmpImage|// first prepare raw data for HTmpImage|' src/iconlist.cpp

# Line 982: 256 colors or less
sed -i '982s|// 256 barev nebo mene: misto blendeni prekryvame sachovnici|// 256 colors or less: instead of blending we overlay a checkerboard|' src/iconlist.cpp

# Line 1000-1001: XOR transparent area
sed -i '1002s|// XOR && pruhledna oblast|// XOR && transparent area|' src/iconlist.cpp

# Line 1013-1014: Transparent area
sed -i '1013s|// pruhledna oblast|// transparent area|' src/iconlist.cpp

# Line 1035: More than 256 colors
sed -i '1035s|// vice nez 256 barev: blednime pomoci alfa kanalu|// more than 256 colors: blend using alpha channel|' src/iconlist.cpp

# Line 1054: XOR transparent area
sed -i '1054s|// XOR && pruhledna oblast|// XOR && transparent area|' src/iconlist.cpp

# Line 1089: Draw HTmpImage to screen
sed -i '1089s|// vykreslime HTmpImage do obrazovky|// draw HTmpImage to screen|' src/iconlist.cpp

# Line 1115: Only for normal icons
sed -i '1115s|// pouze u normalnich ikon ma smysl nastavovat barvu pozadi|// only for normal icons does it make sense to set background color|' src/iconlist.cpp

# Line 1148: Parameter check
sed -i '1148s|// kontrola parametru|// parameter check|' src/iconlist.cpp

# Line 1170-1172: Version using direct data access
sed -i '1170s|// verze kopirovani pomoci primeho pristupu k datum, vyhodou by mela byt|// version using direct data access, the advantage should be|' src/iconlist.cpp
sed -i '1171s|// vyssi rychlost a naprosto identicka kopie (fce BitBlt by mohla zahazovat|// higher speed and absolutely identical copy (BitBlt function could discard|' src/iconlist.cpp
sed -i '1172s|// alpha kanal)|// alpha channel)|' src/iconlist.cpp

# Line 1195: Version using BitBlt
sed -i '1195s|// verze kopirovani pomoci BitBlt|// version using BitBlt|' src/iconlist.cpp

# Line 1226: Allocate bitmap
sed -i '1226s|// naalokujeme bitmapu|// allocate bitmap|' src/iconlist.cpp

# Line 1230: Add stripes from source bitmap
sed -i '1230s|// po radcich do ni pridame prouzky ze zdrojove bitmapy|// add stripes from source bitmap row by row|' src/iconlist.cpp

# Line 1306: According to MSDN
sed -i '1306s|// podle MSDN je treba zavolat nez zacneme pristupovat na raw data|// according to MSDN it is necessary to call before we start accessing raw data|' src/iconlist.cpp

# Line 1308: Set alpha channel according to transparent color
sed -i '1308s|// podle transparentni barvy nastavime alpha kanal|// set alpha channel according to transparent color|' src/iconlist.cpp

# Line 1854: Coordinates in points
sed -i '1854s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 1889: Coordinates in points
sed -i '1889s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 1925: Coordinates in points
sed -i '1925s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 1962: Coordinates in points
sed -i '1962s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 2001: Coordinates in points
sed -i '2001s|// souradnice v bodech v HImage|// coordinates in points in HImage|' src/iconlist.cpp

# Line 2093: Prepare icons in one long row
sed -i '2093s|// pripravime ikony do jednoho dlouheho radku|// prepare icons in one long row|' src/iconlist.cpp

# Line 2100: Coordinates in points in source
sed -i '2100s|// souradnice v bodech ve zdroji|// coordinates in points in source|' src/iconlist.cpp

# Line 2122: Prepare double the memory
sed -i '2122s|// radeji pripravim dvojnasobek pameti, tam se PNG MUSI vejit|// better prepare double the memory, the PNG MUST fit there|' src/iconlist.cpp

echo "Translation complete!"
