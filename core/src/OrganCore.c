/* Orgue à roues phoniques : liaison avec le moteur de setBfree. Voir OrganCore.h.
 *
 * Ce fichier remplace, pour le moteur, ce que setBfree fait avec ses fichiers midi.c, program.c, state.c et cfgParser.c : un
 * petit registre de « fonctions de contrôle » (nom -> fonction), et les aides de lecture de configuration. */
#include "neptune/core/OrganCore.h"

#include <assert.h>
#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "global_inst.h"
#include "main.h"
#include "midi.h"
#include "cfgParser.h"

double SampleRateD = 48000.0; /* setBfree : fréquence d'échantillonnage globale au processus (même valeur pour toutes les instances) */

#define NB3_MAX_CONTROLS 160
#define NB3_FRAGMENT BUFFER_SIZE_SAMPLES

typedef struct
{
	char name[48];
	void (*f) (void*, unsigned char);
	void* d;
} nb3_control_entry;

struct nb3_engine
{
	b_instance inst;
	nb3_control_entry controls[NB3_MAX_CONTROLS];
	int numControls;
	float bufA[NB3_FRAGMENT], bufB[NB3_FRAGMENT], bufC[NB3_FRAGMENT];
	float outL[NB3_FRAGMENT], outR[NB3_FRAGMENT];
	int offset; /* prochain échantillon à rendre dans outL / outR (NB3_FRAGMENT : fragment épuisé) */
	float leslieMix, gain;
};

/* ---- registre de fonctions de contrôle : ce que setBfree demande à son module MIDI ---- */

void useMIDIControlFunction (void* m, const char* cfname, void (*f) (void*, unsigned char), void* d)
{
	nb3_engine* e = (nb3_engine*)m;
	if (e == NULL || e->numControls >= NB3_MAX_CONTROLS || cfname == NULL) return;
	nb3_control_entry* c = &e->controls[e->numControls++];
	strncpy (c->name, cfname, sizeof c->name - 1);
	c->name[sizeof c->name - 1] = '\0';
	c->f = f;
	c->d = d;
}

/* Les modules signalent leurs changements aux contrôleurs MIDI (retour vers les surfaces de contrôle) : sans objet ici. */
void notifyControlChangeByName (void* mcfg, const char* cfname, unsigned char val) { (void)mcfg; (void)cfname; (void)val; }
void notifyControlChangeById (void* mcfg, int id, unsigned char val) { (void)mcfg; (void)id; (void)val; }

/* ---- aides de lecture de la configuration (reprises de cfgParser.c de setBfree, sans l'analyse de fichiers) ---- */



void
showConfigfileContext (ConfigContext* cfg, const char* msg)
{
	assert (cfg != NULL);
	assert (msg != NULL);

	fprintf (stderr,
	         "%s:line %d:name %s:value %s:%s\n",
	         cfg->fname,
	         cfg->linenr,
	         cfg->name,
	         cfg->value,
	         msg);
}

void
configIntUnparsable (ConfigContext* cfg)
{
	assert (cfg != NULL);
	showConfigfileContext (cfg, "value is not an integer");
}

void
configDoubleUnparsable (ConfigContext* cfg)
{
	assert (cfg != NULL);
	showConfigfileContext (cfg, "value is not a real");
}

void
configDoubleOutOfRange (ConfigContext* cfg,
                        double         lowest,
                        double         highest)
{
	char buf[256];
	assert (cfg != NULL);
	sprintf (buf, "real value out of range (%lf -- %lf)", lowest, highest);
	showConfigfileContext (cfg, buf);
}

void
configIntOutOfRange (ConfigContext* cfg, int lowest, int highest)
{
	char buf[256];
	assert (cfg != NULL);
	sprintf (buf, "integer value out of range (%d -- %d)", lowest, highest);
	showConfigfileContext (cfg, buf);
}

/*
 * Assigns an sample range integer from the configuration context's value.
 * If the value is real (contains a period), it is interpreted as a
 * normalised quantity (-1.0 -- 1.0) and multiplied with 32767.0.
 * Otherwise the value is interpreted to be an integer and is assigned
 * as it is.
 */
void
setConfigRangeInt (int* vp, ConfigContext* cfg)
{
	assert (vp != NULL);
	assert (cfg != NULL);

	if (strchr (cfg->value, '.') != NULL) {
		LOCALEGUARD_START;
		double d;
		if (sscanf (cfg->value, "%lf", &d) == 1) {
			*vp = (int)(32767.0 * d);
		} else {
			configDoubleUnparsable (cfg);
		}
		LOCALEGUARD_END;
	} else {
		int k;
		if (sscanf (cfg->value, "%d", &k) == 1) {
			*vp = k;
		} else {
			configIntUnparsable (cfg);
		}
	}
}

/*
 * Assigns an integer from the configuration context's value.
 */
void
setConfigInt (int* vp, ConfigContext* cfg)
{
	int k;
	assert (vp != NULL);
	assert (cfg != NULL);
	if (sscanf (cfg->value, "%d", &k) == 1) {
		*vp = k;
	} else {
		configIntUnparsable (cfg);
	}
}

/*
 * Assigns a double from the configuration context's value.
 */
void
setConfigDouble (double* vp, ConfigContext* cfg)
{
	double d;
	assert (vp != NULL);
	assert (cfg != NULL);
	LOCALEGUARD_START;
	if (sscanf (cfg->value, "%lf", &d) == 1) {
		*vp = d;
	} else {
		configDoubleUnparsable (cfg);
	}
	LOCALEGUARD_END;
}

/*
 * Returns the configuration entry's value string.
 */
const char*
getConfigValue (ConfigContext* cfg)
{
	return cfg->value;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param dp   Pointer the double to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_d (const char* par, ConfigContext* cfg, double* dp)
{
	assert (par != NULL);
	assert (cfg != NULL);
	assert (dp != NULL);

	if (strcasecmp (cfg->name, par) == 0) {
		LOCALEGUARD_START;
		double a;
		if (sscanf (cfg->value, "%lf", &a) == 1) {
			*dp = a;
		} else {
			configDoubleUnparsable (cfg);
			LOCALEGUARD_END;
			return -1;
		}
		LOCALEGUARD_END;
		return 1;
	}
	return 0;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param ip   Pointer the int to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_i (const char* par, ConfigContext* cfg, int* ip)
{
	assert (par != NULL);
	assert (cfg != NULL);
	assert (ip != NULL);

	if (strcasecmp (cfg->name, par) == 0) {
		int i;
		if (sscanf (cfg->value, "%d", &i) == 1) {
			*ip = i;
		} else {
			configIntUnparsable (cfg);
			return -1;
		}
		return 1;
	}
	return 0;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param fp   Pointer the float to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_f (const char* par, ConfigContext* cfg, float* fp)
{
	assert (par != NULL);
	assert (cfg != NULL);
	assert (fp != NULL);

	if (strcasecmp (cfg->name, par) == 0) {
		LOCALEGUARD_START;
		float a;
		if (sscanf (cfg->value, "%f", &a) == 1) {
			*fp = a;
		} else {
			configDoubleUnparsable (cfg);
			LOCALEGUARD_END;
			return -1;
		}
		LOCALEGUARD_END;
		return 1;
	}
	return 0;
}

/*
 * Range-checking version of getConfigParameter_i(...)
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param fp   Pointer the float to set if a match is found.
 * @param lowInc Low limit (inclusive)
 * @param highInc High limit (inclusive)
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_ir (const char*    par,
                       ConfigContext* cfg,
                       int*           ip,
                       int            lowInc,
                       int            highInc)
{
	int k;
	int rtn;

	assert (ip != NULL);
	assert (lowInc <= highInc);

	rtn = getConfigParameter_i (par, cfg, &k);
	if (rtn == 1) {
		if ((lowInc <= k) && (k <= highInc)) {
			*ip = k;
		} else {
			fprintf (stderr, "Value for config parameter %s is out range (%d--%d).",
			         cfg->name,
			         lowInc,
			         highInc);
			rtn = -1;
		}
	}
	return rtn;
}

/*
 * Double-type with range-checking.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_dr (const char*    par,
                       ConfigContext* cfg,
                       double*        dp,
                       double         lowInc,
                       double         highInc)
{
	double d;
	int    rtn;

	assert (dp != NULL);
	assert (lowInc <= highInc);

	rtn = getConfigParameter_d (par, cfg, &d);
	if (rtn == 1) {
		if ((lowInc <= d) && (d <= highInc)) {
			*dp = d;
		} else {
			configDoubleOutOfRange (cfg, lowInc, highInc);
			rtn = -1;
		}
	}
	return rtn;
}

/*
 * Float-type with range-checking.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int
getConfigParameter_fr (const char*    par,
                       ConfigContext* cfg,
                       float*         fp,
                       float          lowInc,
                       float          highInc)
{
	double d;
	int    rtn;

	assert (fp != NULL);
	assert (lowInc <= highInc);

	rtn = getConfigParameter_dr (par, cfg, &d, (double)lowInc, (double)highInc);
	if (rtn == 1) {
		*fp = d;
	}
	return rtn;
}



/* ---- moteur ---- */

#include "program.h"

static void
nb3_free_parts (nb3_engine* e)
{
	if (e->inst.reverb) freeReverb (e->inst.reverb);
	if (e->inst.whirl) freeWhirl (e->inst.whirl);
	if (e->inst.synth) freeToneGenerator (e->inst.synth);
	if (e->inst.preamp) freePreamp (e->inst.preamp);
}

static nb3_engine*
nb3_create_impl (double sampleRate, double refA_Hz, const nb3_wiring* wiring, const nb3_click* click)
{
	nb3_engine* e = (nb3_engine*)calloc (1, sizeof (nb3_engine));
	if (e == NULL) return NULL;
	if (sampleRate > 0.0) SampleRateD = sampleRate;
	e->inst.reverb = allocReverb ();
	e->inst.whirl = allocWhirl ();
	e->inst.synth = allocTonegen ();
	e->inst.preamp = allocPreamp ();
	e->inst.midicfg = e; /* le « module MIDI » est notre registre de fonctions de contrôle */
	if (e->inst.reverb == NULL || e->inst.whirl == NULL || e->inst.synth == NULL || e->inst.preamp == NULL)
	{
		nb3_free_parts (e);
		free (e);
		return NULL;
	}
	/* l'accord et le câblage se fixent avant la construction des tables d'ondes */
	if (refA_Hz >= 220.0 && refA_Hz <= 880.0) setTuning (e->inst.synth, refA_Hz);
	if (wiring != NULL)
	{
		int b, o;
		if (!wiring->foldback) setToneGeneratorModel (e->inst.synth, TG_91FB00);
		e->inst.synth->userTaperOn = wiring->taperOn ? 1 : 0;
		for (b = 0; b < 9; ++b)
			for (o = 0; o < 5; ++o) e->inst.synth->userTaperDb[b][o] = wiring->taperDb[b][o];
	}
	if (click != NULL)
	{
		/* les durées d'origine (8 et 40 échantillons à 22050 Hz) sont calculées par initToneGenerator quand elles valent -1 ; ici on les fixe d'avance */
		const double fs = SampleRateD > 0.0 ? SampleRateD : 48000.0;
		double scale = click->burstScale;
		int lo, hi;
		if (scale < 0.1) scale = 0.1;
		if (scale > 4.0) scale = 4.0;
		lo = (int)floor (fs * 8.0 / 22050.0 * scale);
		hi = (int)ceil (fs * 40.0 / 22050.0 * scale);
		if (hi > BUFFER_SIZE_SAMPLES) hi = BUFFER_SIZE_SAMPLES;
		if (lo > hi - 1) lo = hi - 1;
		if (lo < 0) lo = 0;
		setEnvAttackClickLevel (e->inst.synth, click->attackLevel);
		e->inst.synth->envAtkClkMinLength = lo;
		e->inst.synth->envAtkClkMaxLength = hi;
		if (click->releaseLevel > 0.0f)
		{
			setEnvReleaseModel (e->inst.synth, ENV_CLICK);
			setEnvReleaseClickLevel (e->inst.synth, click->releaseLevel);
		}
	}
	/* équivalent de initSynth() de setBfree (b_synth/lv2.c) sans le module MIDI, les programmes ni l'état courant */
	initToneGenerator (e->inst.synth, e);
	initVibrato (e->inst.synth, e);
	initPreamp (e->inst.preamp, e);
	initReverb (e->inst.reverb, e, SampleRateD);
	initWhirl (e->inst.whirl, e, SampleRateD);
	{
		unsigned int def[9] = { 8, 8, 6, 0, 0, 0, 0, 0, 0 };
		setDrawBars (&e->inst, 0, def);
	}
	e->offset = NB3_FRAGMENT;
	e->leslieMix = 1.0f;
	e->gain = 1.0f;
	return e;
}

nb3_engine* nb3_create (double sampleRate) { return nb3_create_impl (sampleRate, 0.0, NULL, NULL); }
nb3_engine* nb3_create_tuned (double sampleRate, double refA_Hz) { return nb3_create_impl (sampleRate, refA_Hz, NULL, NULL); }
nb3_engine* nb3_create_ex (double sampleRate, double refA_Hz, const nb3_wiring* wiring) { return nb3_create_impl (sampleRate, refA_Hz, wiring, NULL); }
nb3_engine* nb3_create_full (double sampleRate, double refA_Hz, const nb3_wiring* wiring, const nb3_click* click) { return nb3_create_impl (sampleRate, refA_Hz, wiring, click); }

void
nb3_destroy (nb3_engine* e)
{
	if (e == NULL) return;
	nb3_free_parts (e);
	free (e);
}

void nb3_key_on (nb3_engine* e, int key) { if (e != NULL && key >= 0 && key < MAX_KEYS) oscKeyOn (e->inst.synth, (unsigned char)key, (unsigned char)key); }
void nb3_key_off (nb3_engine* e, int key) { if (e != NULL && key >= 0 && key < MAX_KEYS) oscKeyOff (e->inst.synth, (unsigned char)key, (unsigned char)key); }

void
nb3_all_keys_off (nb3_engine* e)
{
	int i;
	if (e == NULL) return;
	for (i = 0; i < MAX_KEYS; ++i) oscKeyOff (e->inst.synth, (unsigned char)i, (unsigned char)i);
}

void
nb3_set_drawbars (nb3_engine* e, int manual, const int values[9])
{
	unsigned int v[9];
	int i;
	if (e == NULL || manual < 0 || manual > 2) return;
	for (i = 0; i < 9; ++i) v[i] = (unsigned int)(values[i] < 0 ? 0 : values[i] > 8 ? 8 : values[i]);
	setDrawBars (&e->inst, (unsigned int)manual, v);
}

int
nb3_control (nb3_engine* e, const char* name, int value)
{
	int i;
	if (e == NULL || name == NULL) return 0;
	for (i = 0; i < e->numControls; ++i)
		if (strcmp (e->controls[i].name, name) == 0)
		{
			e->controls[i].f (e->controls[i].d, (unsigned char)(value < 0 ? 0 : value > 127 ? 127 : value));
			return 1;
		}
	return 0;
}

int nb3_control_count (const nb3_engine* e) { return e != NULL ? e->numControls : 0; }
const char* nb3_control_name (const nb3_engine* e, int index) { return e != NULL && index >= 0 && index < e->numControls ? e->controls[index].name : NULL; }

int
nb3_config (nb3_engine* e, const char* key, const char* value)
{
	ConfigContext cfg;
	int ack = 0;
	if (e == NULL || key == NULL || value == NULL) return 0;
	cfg.fname = "neptune";
	cfg.linenr = 0;
	cfg.name = key;
	cfg.value = value;
	/* même répartition que distributeParameter() de cfgParser.c : chaque module reconnaît ses clés */
	ack += oscConfig (e->inst.synth, &cfg);
	ack += scannerConfig (e->inst.synth, &cfg);
	ack += ampConfig (e->inst.preamp, &cfg);
	ack += reverbConfig (e->inst.reverb, &cfg);
	ack += whirlConfig (e->inst.whirl, &cfg);
	return ack > 0;
}

void
nb3_set_leslie (nb3_engine* e, const nb3_leslie* p)
{
	struct b_whirl* w;
	if (e == NULL || p == NULL) return;
	w = e->inst.whirl;
	w->hornRPMslow = p->hornSlowRpm;
	w->hornRPMfast = p->hornFastRpm;
	w->drumRPMslow = p->drumSlowRpm;
	w->drumRPMfast = p->drumFastRpm;
	w->hornAcc = p->hornAcc;
	w->hornDec = p->hornDec;
	w->drumAcc = p->drumAcc;
	w->drumDec = p->drumDec;
	w->hornLevel = p->hornLevel;
	computeRotationSpeeds (w); /* recalcule les vitesses cibles de chaque combinaison (sans allocation) */
}

void
nb3_set_vibrato_depth (nb3_engine* e, float scale)
{
	struct b_vibrato* v;
	const double S = 65536.0;
	int i;
	double a1, a2, a3;
	if (e == NULL) return;
	v = &e->inst.synth->inst_vibrato;
	scale = scale < 0.0f ? 0.0f : scale > 2.0f ? 2.0f : scale;
	a1 = 3.0 * scale; a2 = 6.0 * scale; a3 = 9.0 * scale;
	if (a1 > 12.0) a1 = 12.0;
	if (a2 > 12.0) a2 = 12.0;
	if (a3 > 12.0) a3 = 12.0;
	v->vib1OffAmp = a1; v->vib2OffAmp = a2; v->vib3OffAmp = a3;
	/* mêmes tables que initIncrementTables() de vibrato.c, sans vider le tampon de retard (pas de clic pendant un balayage) */
	for (i = 0; i < INCTBL_SIZE; i++) {
		const double m = sin ((2.0 * M_PI * i) / INCTBL_SIZE);
		v->offset1Table[i] = (unsigned int)((1.0 + a1 + (m * a1)) * S);
		v->offset2Table[i] = (unsigned int)((1.0 + a2 + (m * a2)) * S);
		v->offset3Table[i] = (unsigned int)((1.0 + a3 + (m * a3)) * S);
	}
}

void nb3_set_leslie_mix (nb3_engine* e, float mix) { if (e != NULL) e->leslieMix = mix < 0.0f ? 0.0f : mix > 1.0f ? 1.0f : mix; }
void nb3_set_output_gain (nb3_engine* e, float gain) { if (e != NULL) e->gain = gain < 0.0f ? 0.0f : gain; }

void
nb3_process (nb3_engine* e, float* left, float* right, int n)
{
	int written = 0;
	if (e == NULL || left == NULL || n <= 0) return;
	while (written < n)
	{
		int i, nread;
		if (e->offset >= NB3_FRAGMENT)
		{
			e->offset = 0;
			oscGenerateFragment (e->inst.synth, e->bufA, NB3_FRAGMENT);
			preamp (e->inst.preamp, e->bufA, e->bufB, NB3_FRAGMENT);
			reverb (e->inst.reverb, e->bufB, e->bufC, NB3_FRAGMENT);
			whirlProc (e->inst.whirl, e->bufC, e->outL, e->outR, NB3_FRAGMENT);
			if (e->leslieMix < 1.0f)
				for (i = 0; i < NB3_FRAGMENT; ++i)
				{
					const float dry = e->bufC[i] * (1.0f - e->leslieMix);
					e->outL[i] = dry + e->outL[i] * e->leslieMix;
					e->outR[i] = dry + e->outR[i] * e->leslieMix;
				}
		}
		nread = NB3_FRAGMENT - e->offset;
		if (nread > n - written) nread = n - written;
		for (i = 0; i < nread; ++i)
		{
			left[written + i] = e->outL[e->offset + i] * e->gain;
			if (right != NULL) right[written + i] = e->outR[e->offset + i] * e->gain;
		}
		written += nread;
		e->offset += nread;
	}
}
