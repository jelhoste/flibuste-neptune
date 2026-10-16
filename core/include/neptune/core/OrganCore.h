#pragma once
// Orgue à roues phoniques : fine enveloppe C autour du moteur de setBfree (third_party/setbfree, GPL v2 ou ultérieure). Tout ce
// qui vient de setBfree reste de l'autre côté de ce fichier : le reste de Neptune ne voit que ces fonctions.
//  - un moteur = un orgue complet (générateur de roues, scanner vibrato / chorus, préampli / overdrive, réverbération à ressorts,
//    cabine Leslie), indépendant des autres instances ; seule la fréquence d'échantillonnage est globale au processus ;
//  - les réglages passent par les « fonctions de contrôle » de setBfree (valeurs 0..127, comme des contrôleurs MIDI), repérées par
//    leur nom ; `nb3_control_names` les liste.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct nb3_engine nb3_engine;

// Hors temps réel (alloue les tables d'ondes). NULL en cas d'échec. `refA_Hz` : accord du la (220..880 ; 0 : 440) ; il fixe les
// tables d'ondes, donc il se choisit à la création.
nb3_engine* nb3_create (double sampleRate);
nb3_engine* nb3_create_tuned (double sampleRate, double refA_Hz);
void nb3_destroy (nb3_engine*);

// Touche du clavier de setBfree : 0..60 manuel supérieur, 64..124 inférieur, 128..159 pédalier.
void nb3_key_on (nb3_engine*, int key);
void nb3_key_off (nb3_engine*, int key);
void nb3_all_keys_off (nb3_engine*);

// Tirettes : manuel 0 supérieur, 1 inférieur, 2 pédalier ; 9 positions 0..8 (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1').
void nb3_set_drawbars (nb3_engine*, int manual, const int values[9]);

// Fonction de contrôle par son nom (« percussion.enable », « vibrato.knob », « rotary.speed-preset », « overdrive.character »...),
// valeur 0..127. Retourne 0 si le nom est inconnu.
int nb3_control (nb3_engine*, const char* name, int value);
int nb3_control_count (const nb3_engine*);
const char* nb3_control_name (const nb3_engine*, int index);

// Réglage par clé de configuration de setBfree (« osc.tuning », « whirl.horn.slowrpm »...) : valeur en texte. Hors temps réel
// (certains réglages recalculent des tables). Retourne 0 si la clé n'est reconnue par aucun module.
int nb3_config (nb3_engine*, const char* key, const char* value);

// Mélange de la cabine Leslie : 0 = signal sec, 1 = Leslie seul. Volume de sortie : gain linéaire.
void nb3_set_leslie_mix (nb3_engine*, float mix);
void nb3_set_output_gain (nb3_engine*, float gain);

// Cabine Leslie : vitesses (tours par minute), temps d'accélération et de décélération (secondes, constante de temps) du cornet et du
// tambour, niveau du cornet (0..1). Sans allocation : utilisable dans le thread audio.
typedef struct
{
	float hornSlowRpm, hornFastRpm, drumSlowRpm, drumFastRpm;
	float hornAcc, hornDec, drumAcc, drumDec;
	float hornLevel;
} nb3_leslie;
void nb3_set_leslie (nb3_engine*, const nb3_leslie*);

// Profondeur du vibrato / chorus : 1 = réglage d'origine de setBfree (modulation 3, 6 et 9), 0 = aucune, plafonnée à 2. Sans allocation.
void nb3_set_vibrato_depth (nb3_engine*, float scale);

// Écrase left / right avec n échantillons (n quelconque : le moteur travaille par fragments de 128).
void nb3_process (nb3_engine*, float* left, float* right, int n);

#ifdef __cplusplus
}
#endif
