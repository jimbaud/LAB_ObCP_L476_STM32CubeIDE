Afin de coder notre obcp en python, il faut d'abord flasher la carte avec le fichier DFU présent dans le dossier.
Pour cela on commence par mettre la carte en mode "prog" puis on la branche et on l'allume.

La seconde etape consiste lancer une console windows et se diriger dans le dossier "dfu-util-0.9-win64" en utilisant la commande "cd"
La commande que j'ai utilisé pour ma part est la suivant : 

cd Downloads\Tuto_Micropython_v1\dfu-util-0.9-win64\dfu-util-0.9-win64

Enfin on flash notre carte en utilisant l'utilitaire dfu-utils : 
La commande de base est la suivante et il faut adapter cette commande en allant copier coller le lien du fichier .dfu comme montré dans la dernière commande


dfu-util --alt 0 -D nom_fichier_firmware.dfu


dfu-util --alt 0 -D C:\Users\natha\Downloads\Tuto_Micropython_v1\NUCLEO_L476RG-20211006-unstable-v1.17-74-gd42cba0d2.dfu

Une fois cette opération réalisée, on peut mettre hors tension la carte et passer en mode "run". 

En mettant alors sous tension la carte celle-ci apparait comme une clé usb comportant divers fichiers.
Les deux fichiers qui nous intéressent sont le boot et le main. 
Le boot est le premier fichier python lancé par la carte.
Le main est l'endroit où le code python doit être écrit.

Deux fichiers exemples sont donnés dans le dossier du tuto

Votre carte est maintenant correctement configurée. Afin d'utiliser les pins de la carte il faut bien déclarer leurs nom en utilisant
le nom présent sur le schéma électrique : par exemple la  Pin A0 a pour nom "PA0"


ATTENTION: lors de la déconnexion de la carte de manière brutale (sans utiliser "éjecter le périphérique") il se peut que le boot et le main soient vides. il faut veiller à sauvegarder son boot et son main en local pour ne pas perdre le code écrit.

PS1: Il semblerait que le boot ne soit pas necessaire pour le bon fonctionnement du programme dans le main.py

PS2: L'environnement pycharm est configurable pour faire fonctionner la carte en "direct" en utilisant le port série et pour coder en python avant d'envoyer le code dans l'obcp.

PS3 : Toute la documentation utilse est présente à cette adresse : https://docs.micropython.org/en/latest/

PS4 : le logiciel Putty est un utilitaire permettant de lancer la communication serie entre la carte et l'ordinateur, efficace dans un premier temps pour faire les premiers tests avec la carte et des leds par exemple ou encore avoir un retour d'informations depuis la carte. Le fichier d'installation est donné dans le dossier de ce tuto



