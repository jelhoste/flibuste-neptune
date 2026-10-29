# Réglages d'une piste de Neptune, pour la Façade

Table générée par `neptune_param_tool` (dossier `tools/param_tool`) : à relancer quand des réglages changent (`neptune_param_tool > docs/parametres-facade.md` après avoir remis l'en-tête). Elle donne, pour la **piste 1** (indice 0 dans le fichier), l'identifiant de chaque réglage qu'un widget de la Façade peut lier, son nom, son type, sa plage et sa valeur par défaut.

- Un widget lie un réglage par son **identifiant** (`"param": 9027`). Une Façade faite avec les identifiants de la piste 1 se recharge sur n'importe quelle piste : Neptune rebranche les identifiants sur la piste cible.
- Les réglages 100 à 106 (Sampler : gain, accord, deux filtres) sont ceux de la piste 1 seulement ; Neptune les rebranche aussi.
- Ne figurent pas ici : les réglages à l'intérieur des inserts, des bus, du Master et des pistes parallèles. Ils se lient dans Neptune (le sélecteur « Choisir le paramètre à lier »).
- Le « Type » dit comment le widget se comporte : *continu* (valeur dans une plage), *pas* (entiers), *interrupteur* (0 ou 1), *choix* (une liste numérotée à partir de 0).

## Réglages de la piste 1

| Identifiant | Nom | Type | Plage | Défaut |
|---|---|---|---|---|
| 100 | Gain | continu | -60 à 12 dB | 0 |
| 101 | Tune (semitones) | pas | -24 à 24 demi-tons | 0 |
| 102 | Fine tune | continu | -100 à 100 cents | 0 |
| 103 | Filter 1 Cutoff | continu | 20 à 20000 Hz | 20000 |
| 104 | Filter 1 Reso | continu | 0 à 100 % | 0 |
| 105 | Filter 2 Cutoff | continu | 20 à 20000 Hz | 20000 |
| 106 | Filter 2 Reso | continu | 0 à 100 % | 0 |
| 1000 | Track 1 Volume | continu | -100 à 6 dB | 0 |
| 1001 | Track 1 Pan | continu | -100 à 100 % | 0 |
| 1002 | Track 1 Mute | interrupteur | 0 ou 1 | 0 |
| 1003 | Track 1 Solo | interrupteur | 0 ou 1 | 0 |
| 1004 | Track 1 Type | choix | 0=Sampler / 1=Subtractive / 2=FM / 3=Audio In | 0 |
| 1010 | Track 1 Insert 1 Bypass | interrupteur | 0 ou 1 | 0 |
| 1011 | Track 1 Insert 2 Bypass | interrupteur | 0 ou 1 | 0 |
| 1012 | Track 1 Insert 3 Bypass | interrupteur | 0 ou 1 | 0 |
| 1013 | Track 1 Insert 4 Bypass | interrupteur | 0 ou 1 | 0 |
| 1014 | Track 1 Insert 5 Bypass | interrupteur | 0 ou 1 | 0 |
| 1015 | Track 1 Insert 6 Bypass | interrupteur | 0 ou 1 | 0 |
| 1016 | Track 1 Insert 7 Bypass | interrupteur | 0 ou 1 | 0 |
| 1017 | Track 1 Insert 8 Bypass | interrupteur | 0 ou 1 | 0 |
| 1020 | Track 1 Send 1 Level | continu | -100 à 6 dB | -100 |
| 1021 | Track 1 Send 2 Level | continu | -100 à 6 dB | -100 |
| 1022 | Track 1 Send 3 Level | continu | -100 à 6 dB | -100 |
| 1023 | Track 1 Send 4 Level | continu | -100 à 6 dB | -100 |
| 1024 | Track 1 Send 5 Level | continu | -100 à 6 dB | -100 |
| 1025 | Track 1 Send 6 Level | continu | -100 à 6 dB | -100 |
| 1026 | Track 1 Send 7 Level | continu | -100 à 6 dB | -100 |
| 1027 | Track 1 Send 8 Level | continu | -100 à 6 dB | -100 |
| 1040 | Track 1 Osc 1 Level | continu | 0 à 100 % | 100 |
| 1041 | Track 1 Osc 2 Level | continu | 0 à 100 % | 0 |
| 1042 | Track 1 Osc 3 Level | continu | 0 à 100 % | 0 |
| 1043 | Track 1 Sub Level | continu | 0 à 100 % | 0 |
| 1044 | Track 1 Noise Level | continu | 0 à 100 % | 0 |
| 1045 | Track 1 Osc 1 Fine | continu | -100 à 100 cents | 0 |
| 1046 | Track 1 Osc 2 Fine | continu | -100 à 100 cents | 0 |
| 1047 | Track 1 Osc 3 Fine | continu | -100 à 100 cents | 0 |
| 1048 | Track 1 Osc 1 Pulse Width | continu | 2 à 98 % | 50 |
| 1049 | Track 1 Osc 2 Pulse Width | continu | 2 à 98 % | 50 |
| 1050 | Track 1 Osc 3 Pulse Width | continu | 2 à 98 % | 50 |
| 1051 | Track 1 Osc 1 Unison Detune | continu | 0 à 100 cents | 20 |
| 1052 | Track 1 Osc 2 Unison Detune | continu | 0 à 100 cents | 20 |
| 1053 | Track 1 Osc 3 Unison Detune | continu | 0 à 100 cents | 20 |
| 1054 | Track 1 Pitch Env Depth | continu | -48 à 48 demi-tons | 0 |
| 1071 | Track 1 Amp Env Attack | continu | 0 à 10000 ms | 5 |
| 1072 | Track 1 Amp Env Decay | continu | 0 à 10000 ms | 0 |
| 1073 | Track 1 Amp Env Sustain | continu | 0 à 100 % | 100 |
| 1074 | Track 1 Amp Env Release | continu | 0 à 10000 ms | 150 |
| 1075 | Track 1 Filter 1 Env Attack | continu | 0 à 10000 ms | 0 |
| 1076 | Track 1 Filter 1 Env Decay | continu | 0 à 10000 ms | 0 |
| 1077 | Track 1 Filter 1 Env Sustain | continu | 0 à 100 % | 100 |
| 1078 | Track 1 Filter 1 Env Release | continu | 0 à 10000 ms | 50 |
| 1079 | Track 1 Filter 2 Env Attack | continu | 0 à 10000 ms | 0 |
| 1080 | Track 1 Filter 2 Env Decay | continu | 0 à 10000 ms | 0 |
| 1081 | Track 1 Filter 2 Env Sustain | continu | 0 à 100 % | 100 |
| 1082 | Track 1 Filter 2 Env Release | continu | 0 à 10000 ms | 50 |
| 1083 | Track 1 Glide | continu | 0 à 2000 ms | 0 |
| 1084 | Track 1 Filter Balance | continu | 0 à 100 % | 50 |
| 1060 | Track 1 FM Brightness | continu | -100 à 100 % | 0 |
| 1061 | Track 1 FM Envelope Speed | continu | -100 à 100 % | 0 |
| 1062 | Track 1 FM Feedback Drive | continu | 0 à 100 % | 0 |
| 1063 | Track 1 FM Op 1 Level | continu | -99 à 99 | 0 |
| 1064 | Track 1 FM Op 2 Level | continu | -99 à 99 | 0 |
| 1065 | Track 1 FM Op 3 Level | continu | -99 à 99 | 0 |
| 1066 | Track 1 FM Op 4 Level | continu | -99 à 99 | 0 |
| 1067 | Track 1 FM Op 5 Level | continu | -99 à 99 | 0 |
| 1068 | Track 1 FM Op 6 Level | continu | -99 à 99 | 0 |
| 1069 | Track 1 FM Unison Detune | continu | 0 à 100 cents | 15 |
| 1070 | Track 1 FM Unison Spread | continu | 0 à 100 % | 60 |
| 1085 | Track 1 FM LFO Speed | pas | 0 à 99 | 35 |
| 1086 | Track 1 FM LFO Pitch Depth | pas | 0 à 99 | 0 |
| 1087 | Track 1 FM LFO Amp Depth | pas | 0 à 99 | 0 |
| 1088 | Track 1 FM Bend Range | pas | 0 à 12 demi-tons | 2 |
| 1089 | Track 1 FM Wheel Pitch Sens | pas | 0 à 15 | 0 |
| 1090 | Track 1 FM Wheel Amp Sens | pas | 0 à 15 | 0 |
| 1091 | Track 1 FM Glide | continu | 0 à 2000 ms | 0 |
| 9000 | Track 1 Upper 16' | pas | 0 à 8 | 8 |
| 9009 | Track 1 Lower 16' | pas | 0 à 8 | 0 |
| 9001 | Track 1 Upper 5 1/3' | pas | 0 à 8 | 8 |
| 9010 | Track 1 Lower 5 1/3' | pas | 0 à 8 | 0 |
| 9002 | Track 1 Upper 8' | pas | 0 à 8 | 6 |
| 9011 | Track 1 Lower 8' | pas | 0 à 8 | 0 |
| 9003 | Track 1 Upper 4' | pas | 0 à 8 | 0 |
| 9012 | Track 1 Lower 4' | pas | 0 à 8 | 0 |
| 9004 | Track 1 Upper 2 2/3' | pas | 0 à 8 | 0 |
| 9013 | Track 1 Lower 2 2/3' | pas | 0 à 8 | 0 |
| 9005 | Track 1 Upper 2' | pas | 0 à 8 | 0 |
| 9014 | Track 1 Lower 2' | pas | 0 à 8 | 0 |
| 9006 | Track 1 Upper 1 3/5' | pas | 0 à 8 | 0 |
| 9015 | Track 1 Lower 1 3/5' | pas | 0 à 8 | 0 |
| 9007 | Track 1 Upper 1 1/3' | pas | 0 à 8 | 0 |
| 9016 | Track 1 Lower 1 1/3' | pas | 0 à 8 | 0 |
| 9008 | Track 1 Upper 1' | pas | 0 à 8 | 0 |
| 9017 | Track 1 Lower 1' | pas | 0 à 8 | 0 |
| 9018 | Track 1 Pedal 16' | pas | 0 à 8 | 0 |
| 9019 | Track 1 Pedal 8' | pas | 0 à 8 | 0 |
| 9020 | Track 1 Percussion | interrupteur | 0 ou 1 | 0 |
| 9021 | Track 1 Percussion Soft | interrupteur | 0 ou 1 | 0 |
| 9022 | Track 1 Percussion Fast | interrupteur | 0 ou 1 | 1 |
| 9023 | Track 1 Percussion 3rd Harmonic | interrupteur | 0 ou 1 | 1 |
| 9024 | Track 1 Vibrato | choix | 0=Off / 1=V1 / 2=C1 / 3=V2 / 4=C2 / 5=V3 / 6=C3 | 0 |
| 9025 | Track 1 Vibrato Upper | interrupteur | 0 ou 1 | 1 |
| 9026 | Track 1 Vibrato Lower | interrupteur | 0 ou 1 | 0 |
| 9027 | Track 1 Leslie Speed | choix | 0=Chorale / 1=Tremolo / 2=Brake | 0 |
| 9028 | Track 1 Leslie Mix | continu | 0 à 100 % | 100 |
| 9029 | Track 1 Drive | interrupteur | 0 ou 1 | 0 |
| 9030 | Track 1 Drive Input | continu | 0 à 100 % | 50 |
| 9031 | Track 1 Drive Character | continu | 0 à 100 % | 50 |
| 9032 | Track 1 Drive Output | continu | 0 à 100 % | 50 |
| 9033 | Track 1 Reverb | continu | 0 à 100 % | 0 |
| 9034 | Track 1 Vibrato Depth | continu | 0 à 100 % | 50 |
| 12000 | Track 1 Slot 1 | continu | -100 à 100 % | 0 |
| 12001 | Track 1 Slot 2 | continu | -100 à 100 % | 0 |
| 12002 | Track 1 Slot 3 | continu | -100 à 100 % | 0 |
| 12003 | Track 1 Slot 4 | continu | -100 à 100 % | 0 |
| 12004 | Track 1 Slot 5 | continu | -100 à 100 % | 0 |
| 12005 | Track 1 Slot 6 | continu | -100 à 100 % | 0 |
| 12006 | Track 1 Slot 7 | continu | -100 à 100 % | 0 |
| 12007 | Track 1 Slot 8 | continu | -100 à 100 % | 0 |
| 12008 | Track 1 Slot 9 | continu | -100 à 100 % | 0 |
| 12009 | Track 1 Slot 10 | continu | -100 à 100 % | 0 |
| 12010 | Track 1 Slot 11 | continu | -100 à 100 % | 0 |
| 12011 | Track 1 Slot 12 | continu | -100 à 100 % | 0 |
| 12012 | Track 1 Slot 13 | continu | -100 à 100 % | 0 |
| 12013 | Track 1 Slot 14 | continu | -100 à 100 % | 0 |
| 12014 | Track 1 Slot 15 | continu | -100 à 100 % | 0 |
| 12015 | Track 1 Slot 16 | continu | -100 à 100 % | 0 |
| 14000 | Track 1 Modulator 1 | continu | 0 à 100 % | 100 |
| 14001 | Track 1 Modulator 2 | continu | 0 à 100 % | 100 |
| 14002 | Track 1 Modulator 3 | continu | 0 à 100 % | 100 |
| 14003 | Track 1 Modulator 4 | continu | 0 à 100 % | 100 |
| 14004 | Track 1 Modulator 5 | continu | 0 à 100 % | 100 |
| 14005 | Track 1 Modulator 6 | continu | 0 à 100 % | 100 |
| 14006 | Track 1 Modulator 7 | continu | 0 à 100 % | 100 |
| 14007 | Track 1 Modulator 8 | continu | 0 à 100 % | 100 |
| 14008 | Track 1 Modulator 9 | continu | 0 à 100 % | 100 |
| 14009 | Track 1 Modulator 10 | continu | 0 à 100 % | 100 |
| 14010 | Track 1 Modulator 11 | continu | 0 à 100 % | 100 |
| 14011 | Track 1 Modulator 12 | continu | 0 à 100 % | 100 |
| 14012 | Track 1 Modulator 13 | continu | 0 à 100 % | 100 |
| 14013 | Track 1 Modulator 14 | continu | 0 à 100 % | 100 |
| 14014 | Track 1 Modulator 15 | continu | 0 à 100 % | 100 |
| 14015 | Track 1 Modulator 16 | continu | 0 à 100 % | 100 |
| 16000 | Track 1 Modulator 17 | continu | 0 à 100 % | 100 |
| 16001 | Track 1 Modulator 18 | continu | 0 à 100 % | 100 |
| 16002 | Track 1 Modulator 19 | continu | 0 à 100 % | 100 |
| 16003 | Track 1 Modulator 20 | continu | 0 à 100 % | 100 |
| 16004 | Track 1 Modulator 21 | continu | 0 à 100 % | 100 |
| 16005 | Track 1 Modulator 22 | continu | 0 à 100 % | 100 |
| 16006 | Track 1 Modulator 23 | continu | 0 à 100 % | 100 |
| 16007 | Track 1 Modulator 24 | continu | 0 à 100 % | 100 |
| 16008 | Track 1 Modulator 25 | continu | 0 à 100 % | 100 |
| 16009 | Track 1 Modulator 26 | continu | 0 à 100 % | 100 |
| 16010 | Track 1 Modulator 27 | continu | 0 à 100 % | 100 |
| 16011 | Track 1 Modulator 28 | continu | 0 à 100 % | 100 |
| 16012 | Track 1 Modulator 29 | continu | 0 à 100 % | 100 |
| 16013 | Track 1 Modulator 30 | continu | 0 à 100 % | 100 |
| 16014 | Track 1 Modulator 31 | continu | 0 à 100 % | 100 |
| 16015 | Track 1 Modulator 32 | continu | 0 à 100 % | 100 |

### Macros (communes à toutes les pistes)

| Identifiant | Nom | Type | Plage | Défaut |
|---|---|---|---|---|
| 13000 | Macro 1 | continu | 0 à 100 % | 0 |
| 13001 | Macro 2 | continu | 0 à 100 % | 0 |
| 13002 | Macro 3 | continu | 0 à 100 % | 0 |
| 13003 | Macro 4 | continu | 0 à 100 % | 0 |
| 13004 | Macro 5 | continu | 0 à 100 % | 0 |
| 13005 | Macro 6 | continu | 0 à 100 % | 0 |
| 13006 | Macro 7 | continu | 0 à 100 % | 0 |
| 13007 | Macro 8 | continu | 0 à 100 % | 0 |
| 13008 | Macro 9 | continu | 0 à 100 % | 0 |
| 13009 | Macro 10 | continu | 0 à 100 % | 0 |
| 13010 | Macro 11 | continu | 0 à 100 % | 0 |
| 13011 | Macro 12 | continu | 0 à 100 % | 0 |
| 13012 | Macro 13 | continu | 0 à 100 % | 0 |
| 13013 | Macro 14 | continu | 0 à 100 % | 0 |
| 13014 | Macro 15 | continu | 0 à 100 % | 0 |
| 13015 | Macro 16 | continu | 0 à 100 % | 0 |
| 13016 | Macro 17 | continu | 0 à 100 % | 0 |
| 13017 | Macro 18 | continu | 0 à 100 % | 0 |
| 13018 | Macro 19 | continu | 0 à 100 % | 0 |
| 13019 | Macro 20 | continu | 0 à 100 % | 0 |
| 13020 | Macro 21 | continu | 0 à 100 % | 0 |
| 13021 | Macro 22 | continu | 0 à 100 % | 0 |
| 13022 | Macro 23 | continu | 0 à 100 % | 0 |
| 13023 | Macro 24 | continu | 0 à 100 % | 0 |
| 13024 | Macro 25 | continu | 0 à 100 % | 0 |
| 13025 | Macro 26 | continu | 0 à 100 % | 0 |
| 13026 | Macro 27 | continu | 0 à 100 % | 0 |
| 13027 | Macro 28 | continu | 0 à 100 % | 0 |
| 13028 | Macro 29 | continu | 0 à 100 % | 0 |
| 13029 | Macro 30 | continu | 0 à 100 % | 0 |
| 13030 | Macro 31 | continu | 0 à 100 % | 0 |
| 13031 | Macro 32 | continu | 0 à 100 % | 0 |
