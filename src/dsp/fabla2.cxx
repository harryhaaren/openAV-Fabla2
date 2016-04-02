/*
 * Author: Harry van Haaren 2014
 *         harryhaaren@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "fabla2.hxx"

#include "../dsp.hxx"

#ifdef OPENAV_PROFILE
#include "../profiny.hxx"
#endif

#include <sstream>

#include <stdio.h>
#include <string.h>

#include "pad.hxx"
#include "bank.hxx"
#include "voice.hxx"
#include "sample.hxx"
#include "sampler.hxx"
#include "pattern.hxx"
#include "library.hxx"
#include "sequencer.h"
#include "midi_helper.hxx"

#include "plotter.hxx"

namespace Fabla2
{

Fabla2DSP::Fabla2DSP( int rate, URIs* u ) :
	sr( rate ),
	uris( u ),
	useAuxbus( false ),
	patternPlay( false ),
	patternBank( 0 ),
	patternChoice( 0 ),
	dbMeter( rate ),
	recordEnable( false ),
	recordBank( 0 ),
	recordPad( 0 ),
	uiDbUpdateCounter(rate/30)
{
	library = new Library( this, rate );

	auditionVoice = new Voice( this, rate );
	auditionPad = new Pad( this, rate, -1 );

	for( int i = 0; i < 16; i++ )
		voices.push_back( new Voice( this, rate ) );

	recordBuffer.resize( rate * 10 );

	memset( controlPorts, 0, sizeof(float*) * PORT_COUNT );

	int bankID = 0;
	for(int i = 0; i < 16 * 4; i++) {
		if( i > 0 && i % 16 == 0 ) {
			bankID++;
		}

		Pad* tmpPad = new Pad( this, rate, i % 16 );
		tmpPad->bank( i / 16 );

		library->bank( bankID )->pad( tmpPad );
	}

	// for debugging null pointers etc
	//library->checkAll();
}

void Fabla2DSP::stepSeq(int bank, int pad, int step, int value)
{
	Sequencer* s = library->bank(bank)->getPattern()->getSequencer(pad);
	sequencer_set_step(s, step, value);
}

void Fabla2DSP::process( int nf )
{
#ifdef OPENAV_PROFILE
	PROFINY_SCOPE
#endif
	nframes = nf;

	float recordOverLast = *controlPorts[RECORD_OVER_LAST_PLAYED_PAD];
	if( recordEnable != (int)recordOverLast ) {
		// update based on control port value
		recordEnable = (int)recordOverLast;

		if( recordEnable ) {
			printf("recording switch! %i\n", recordEnable );
			startRecordToPad( recordBank, recordPad );
		} else {
			stopRecordToPad();
		}
	}

	// clear the audio buffers
	memset( controlPorts[OUTPUT_L],  0, sizeof(float) * nframes );
	memset( controlPorts[OUTPUT_R],  0, sizeof(float) * nframes );

	// check if the host connected the AuxBus buffers - if so, set
	// useAuxbus to true
	if( !useAuxbus && controlPorts[AUXBUS1_L] && controlPorts[AUXBUS4_R] ) {
		useAuxbus = true;
	}

	// aux buffers if set
	if( useAuxbus ) {
		memset( controlPorts[AUXBUS1_L], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS1_R], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS2_L], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS2_R], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS3_L], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS3_R], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS4_L], 0, sizeof(float) * nframes );
		memset( controlPorts[AUXBUS4_R], 0, sizeof(float) * nframes );
	}

	if( refresh_UI ) {
		int bank = 0;
		for(int i = 0; i < 16; i++) {
			// TODO FIXME: Bank here needs to be the loaded one 
			Pad* p = library->bank( bank )->pad( i );
			if(p) {
				LV2_Atom_Forge_Frame frame;
				lv2_atom_forge_frame_time( &lv2->forge, 0 );
				lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_PadHasSample );
				lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
				lv2_atom_forge_int(&lv2->forge, bank );
				lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
				lv2_atom_forge_int(&lv2->forge, i );
				lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadHasSample);
				lv2_atom_forge_int(&lv2->forge, p->loaded() );
				lv2_atom_forge_pop(&lv2->forge, &frame);
			} else {
				//printf("%s - pad not valid!\n", __PRETTY_FUNCTION__);
			}
		}
		// FIXME Pad = 0
		Pad* p = library->bank( bank )->pad( 0 );
		writePadsState(bank, 0, p );
		writeSampleState(bank, 0, 0, p, p->layer(0) );

		for(int i = 0; i < 4;i++) {
			LV2_Atom_Forge_Frame frame;
			lv2_atom_forge_frame_time( &lv2->forge, 0 );
			lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_AuxBus);
			lv2_atom_forge_key(&lv2->forge, uris->fabla2_auxBusNumber);
			lv2_atom_forge_int(&lv2->forge, i );
			lv2_atom_forge_key(&lv2->forge, uris->fabla2_value);
			lv2_atom_forge_float(&lv2->forge, auxBusVol[i]);
			lv2_atom_forge_pop(&lv2->forge, &frame);
		}

		refresh_UI = 0;
	}

	if( recordEnable && recordPad != -1 && recordIndex + nframes < sr * 4 ) {
		//printf("recording...\n" );
		for(int i = 0; i < nframes; i++) {
			recordBuffer[recordIndex++] = controlPorts[INPUT_L][i];
			recordBuffer[recordIndex++] = controlPorts[INPUT_R][i];
		}
	} else if( recordEnable && recordPad != -1 ) {
		recordEnable = false;
		printf("record stopped: out of space! %li\n", recordIndex );
	}


	float transport_play = *controlPorts[TRANSPORT_PLAY];
	if( transport_play ) {
		// TODO: Refactor to use patternChoice
		Pattern* p = library->bank(patternBank)->getPattern();
		if(patternPlay == false) {
			// start playing, so reset to start of loop/pattern
			p->rewind_to_start();
		}
		p->setBPM( *controlPorts[TRANSPORT_BPM] );
		p->process( nf );
		// keep state of playback
		patternPlay = true;
	}
	else {
		patternPlay = false;
	}

	// Process voices
	for( int i = 0; i < voices.size(); i++ ) {
		Voice* v = voices.at(i);
		if( v->active() ) {
			//printf("voice %i playing\n", i);
			v->process();
		}
	}

	// Process audition voice
	auditionVoice->process();


	uiDbUpdateCounter -= nframes;
	if( uiDbUpdateCounter < 0 ) {
		float* buf[2];
		buf[0] = controlPorts[OUTPUT_L];
		buf[1] = controlPorts[OUTPUT_R];
		dbMeter.process(nf, &buf[0], &buf[0] );
		// send UI message
		LV2_Atom_Forge_Frame frame;
		lv2_atom_forge_frame_time( &lv2->forge, 0 );
		lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_dbMeter );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_dbMeter);
		lv2_atom_forge_int(&lv2->forge, 0 );

		lv2_atom_forge_key(&lv2->forge, uris->fabla2_value);
		lv2_atom_forge_float(&lv2->forge, dbMeter.getLeftDB() );

		lv2_atom_forge_pop(&lv2->forge, &frame);
		uiDbUpdateCounter = sr / 30;
	}
}

void Fabla2DSP::auditionStop()
{
	auditionVoice->stop();
}

Fabla2::Sample* Fabla2DSP::auditionPlay( Fabla2::Sample* sample )
{
	auditionVoice->stop();
	auditionPad->clearAllSamples();
	auditionPad->add(sample);
	auditionVoice->playLayer( auditionPad, 0 );
	return 0;
}

void Fabla2DSP::auditionPlay( int bank, int pad, int layer )
{
	if ( bank < 0 || bank >=  4 ) return;
	if ( pad  < 0 || pad  >= 16 ) return;

	Pad* p = library->bank( bank )->pad( pad );
	Sample* s = p->layer( layer );

	if( !s )
		return;

	writeSampleState( bank, pad, layer, p, s );

	auditionVoice->stop();

	auditionVoice->playLayer( p, layer );
}

static void fabla2_dsp_getDetailsFromNote( const uint8_t* msg, int& bank, int& pad )
{
	// check MIDI note is valid in downwards direction
	if( msg[1] < 36 )
		return;

	// get midi bank (from message, or from MIDI note num?)
	bank = 0; //(msg[0] & 0x0F);

	// select pad number from midi note (0-15)
	pad = (msg[1] - 36) % 16;

	// convert higher midi notes ( > 36 + 16, aka first bank) to next bank
	if( msg[1] >= 36 + 16 ) {
		int nMoreBanks = (msg[1] - 36) / 16;
		//printf( "more banks %i\n", nMoreBanks );
		bank += nMoreBanks;
	}
}

void Fabla2DSP::refreshUI()
{
	refresh_UI = true;
}

void Fabla2DSP::midi( int eventTime, const uint8_t* msg, bool fromUI )
{
	//printf("MIDI: %i, %i, %i\n", (int)msg[0], (int)msg[1], (int)msg[2] );


	switch( lv2_midi_message_type( msg ) ) {
	case LV2_MIDI_MSG_NOTE_ON: {
		// check MIDI note is valid in downwards direction
		if( msg[1] < 36 )
			return;

		int bank = 0;
		int pad  = 0;
		fabla2_dsp_getDetailsFromNote( msg, bank, pad );
		if( bank < 0 || bank >=  4 ) {
			return;
		}
		if( pad  < 0 || pad  >= 16 ) {
			return;
		}

		// update the recording pad
		recordBank = bank;
		recordPad  = pad;

		// the pad that's going to be allocated to play
		Pad* p = library->bank( bank )->pad( pad );

		bool allocd = false;
		for(int i = 0; i < voices.size(); i++) {
			// current voice pointer
			Voice* v = voices.at(i);

			// check mute-group to stop the voice first
			if( v->active() ) {
				int mg = p->muteGroup();
				// note-on mute-group is valid && == to current voice off-group
				if( mg != 0 &&
				    mg == v->getPad()->offGroup() ) {
					// note that this triggers ADSR off, so we can *NOT* re-purpose
					// the voice right away to play the new note.
					//printf("note-on muteGroup %i : turning off %i\n", mg, v->getPad()->offGroup() );
					v->kill();
				}
			} else {
				// only allocate voice if we haven't already done so
				if( !allocd ) {
					// play pad
					voices.at(i)->play( eventTime, bank, pad, p, msg[2] / 127.f );

					// write note on MIDI events to UI
					LV2_Atom_Forge_Frame frame;
					lv2_atom_forge_frame_time( &lv2->forge, eventTime );
					lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_PadPlay );

					lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
					lv2_atom_forge_int(&lv2->forge, bank );
					lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
					lv2_atom_forge_int(&lv2->forge, pad );
					lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
					lv2_atom_forge_int(&lv2->forge, p->lastPlayedLayer() );
					lv2_atom_forge_key(&lv2->forge, uris->fabla2_velocity);
					lv2_atom_forge_int(&lv2->forge, msg[2] );

					lv2_atom_forge_pop(&lv2->forge, &frame);

					padRefreshLayers(bank, pad);
					int l = p->lastPlayedLayer();
					Sample* s = p->layer( l );
					if(s) {
						writeSampleState(bank, pad, l, p, s);
						tx_waveform(bank, pad, l, s->getWaveform());
					}
					allocd = true;
				}
			}
		}
		/// if all voices are full, we steal the first one
		if( allocd == false ) {
			voices.at( 0 )->play( eventTime, bank, pad, p, msg[2] );
		}
	}
	break;

	case LV2_MIDI_MSG_NOTE_OFF: {
		int bank = 0;
		int pad  = 0;
		fabla2_dsp_getDetailsFromNote( msg, bank, pad );
		if( bank < 0 || bank >=  4 ) {
			return;
		}
		if( pad  < 0 || pad  >= 16 ) {
			return;
		}

		// write note on MIDI events to UI
		LV2_Atom_Forge_Frame frame;
		lv2_atom_forge_frame_time( &lv2->forge, eventTime );
		lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_PadStop );

		lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
		lv2_atom_forge_int(&lv2->forge, bank );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
		lv2_atom_forge_int(&lv2->forge, pad );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
		lv2_atom_forge_int(&lv2->forge, -1 );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_velocity);
		lv2_atom_forge_int(&lv2->forge, msg[2] );

		lv2_atom_forge_pop(&lv2->forge, &frame);

		for(int i = 0; i < voices.size(); i++) {
			Voice* v = voices.at(i);

			if( v->active() ) {
				if( v->matches( bank, pad ) ) {
					// this voice was started by the Pad that now sent corresponding
					// note off event: so stop() the voice
					//printf("Voice.matces() NOTE_OFF -> Stop()\n" );
					v->stop();
					return;
				}
			}
		}
	}
	break;

	case LV2_MIDI_MSG_CONTROLLER:
		if( msg[1] == 120 ) // all sounds off
			panic();
		if( msg[1] == 123 ) // all notes off
			panic();
		break;

	case LV2_MIDI_MSG_PGM_CHANGE:
		//printf("MIDI : Program Change received\n");
		break;
	
	default:
		// ALL UN-USED TYPES OF MIDI MESSAGES
		break;
	}

}

void Fabla2DSP::writePadsState( int b, int p, Pad* pad )
{
	assert( pad );

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time( &lv2->forge, 0 );

	lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_UiPadsState );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
	lv2_atom_forge_int(&lv2->forge, b );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
	lv2_atom_forge_int(&lv2->forge, p );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_name);
	const char* padName = pad->getName();
	if(!padName) {
		padName = "Pad-Name-Error";
	}
	lv2_atom_forge_string(&lv2->forge, padName, strlen(padName));

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadVolume );
	lv2_atom_forge_float(&lv2->forge, pad->volume );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_value );
	lv2_atom_forge_float(&lv2->forge, pad->loaded() );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus1 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[0] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus2 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[1] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus3 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[2] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus4 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[3] );

	lv2_atom_forge_pop(&lv2->forge, &frame);
}

void Fabla2DSP::writeSampleState( int b, int p, int l, Pad* pad, Sample* s )
{
	if( !pad || !s )
		return;

	assert( pad );
	assert( s );

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time( &lv2->forge, 0 );

	lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_ReplyUiSampleState );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
	lv2_atom_forge_int(&lv2->forge, b );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
	lv2_atom_forge_int(&lv2->forge, p );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
	lv2_atom_forge_int(&lv2->forge, l );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_name);
	lv2_atom_forge_string(&lv2->forge, s->getName(), strlen( s->getName() ) );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadVolume );
	lv2_atom_forge_float(&lv2->forge, pad->volume );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus1 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[0] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus2 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[1] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus3 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[2] );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadAuxBus4 );
	lv2_atom_forge_float(&lv2->forge, pad->sends[3] );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadMuteGroup);
	lv2_atom_forge_int(&lv2->forge, pad->muteGroup() );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadOffGroup);
	lv2_atom_forge_int(&lv2->forge, pad->offGroup() );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadTriggerMode);
	lv2_atom_forge_int(&lv2->forge, pad->triggerMode() );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_PadSwitchType);
	lv2_atom_forge_int(&lv2->forge, pad->switchSystem() );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleGain);
	lv2_atom_forge_float(&lv2->forge, s->gain );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SamplePitch);
	lv2_atom_forge_float(&lv2->forge, s->pitch );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SamplePan );
	lv2_atom_forge_float(&lv2->forge, s->pan );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleStartPoint );
	lv2_atom_forge_float(&lv2->forge, s->startPoint);

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleVelStartPnt );
	lv2_atom_forge_float(&lv2->forge, s->velLow );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleVelEndPnt );
	lv2_atom_forge_float(&lv2->forge, s->velHigh );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleFilterType );
	lv2_atom_forge_float(&lv2->forge, s->filterType );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleFilterFrequency );
	lv2_atom_forge_float(&lv2->forge, s->filterFrequency );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleFilterResonance );
	lv2_atom_forge_float(&lv2->forge, s->filterResonance );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleAdsrAttack );
	lv2_atom_forge_float(&lv2->forge, s->attack );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleAdsrDecay );
	lv2_atom_forge_float(&lv2->forge, s->decay );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleAdsrSustain );
	lv2_atom_forge_float(&lv2->forge, s->sustain );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_SampleAdsrRelease );
	lv2_atom_forge_float(&lv2->forge, s->release );

	lv2_atom_forge_pop(&lv2->forge, &frame);
}

void Fabla2DSP::padRefreshLayers( int bank, int pad)
{
	//printf("%s, %d %d\n", __PRETTY_FUNCTION__, bank, pad);
	Bank* b = library->bank(bank);
	if( !b ) {
		//printf("%s no bank\n", __PRETTY_FUNCTION__);
		return;
	}
	Pad* p = b->pad( pad );
	if( !p ) {
		//printf("%s  no pad\n", __PRETTY_FUNCTION__);
		return; 
	}

	for(int i = 0; i < p->nLayers(); i++) {
		LV2_Atom_Forge_Frame frame;

		lv2_atom_forge_frame_time(&lv2->forge, 0);
		lv2_atom_forge_object(&lv2->forge, &frame, 0, uris->fabla2_PadRefreshLayers);

		lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
		lv2_atom_forge_int(&lv2->forge, bank );

		lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
		lv2_atom_forge_int(&lv2->forge, pad );

		lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
		lv2_atom_forge_int(&lv2->forge, i );

		/// write this layers sample details:
		/// 1. Name
		/// 2. if Velocity layered mode: Velocity ranges
		const char* name = p->layer( i )->getName();
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_name);
		lv2_atom_forge_string(&lv2->forge, name, strlen( name ) );
		lv2_atom_forge_pop(&lv2->forge, &frame);
	}

	// now *re-write* the note-on event to highlight the played layer in the UI
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time( &lv2->forge, 0 );
	lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_PadPlay );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
	lv2_atom_forge_int(&lv2->forge, bank );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
	lv2_atom_forge_int(&lv2->forge, pad );
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
	lv2_atom_forge_int(&lv2->forge, p->lastPlayedLayer() );

	lv2_atom_forge_pop(&lv2->forge, &frame);
}

void Fabla2DSP::tx_waveform( int b, int p, int l, const float* data )
{
	assert( data );

	LV2_Atom_Forge_Frame frame;

	lv2_atom_forge_frame_time(&lv2->forge, 0);
	lv2_atom_forge_object(&lv2->forge, &frame, 0,
	                      uris->fabla2_SampleAudioData);

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
	lv2_atom_forge_int(&lv2->forge, b );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
	lv2_atom_forge_int(&lv2->forge, p );

	lv2_atom_forge_key(&lv2->forge, uris->fabla2_layer);
	lv2_atom_forge_int(&lv2->forge, l );

	//Plotter::plot( "tx_waveform", FABLA2_UI_WAVEFORM_PX, data );

	// Add vector of floats 'audioData' property
	lv2_atom_forge_key(&lv2->forge, uris->fabla2_audioData);
	lv2_atom_forge_vector( &lv2->forge, sizeof(float), uris->atom_Float,
	                       FABLA2_UI_WAVEFORM_PX, data);

	// Close off object
	lv2_atom_forge_pop(&lv2->forge, &frame);
}

void Fabla2DSP::panic()
{
	for(int i = 0; i < voices.size(); ++i) {
		voices.at(i)->stop();
	}
	auditionStop();
}

void Fabla2DSP::uiMessage(int b, int p, int l, int URI, float v)
{
	//printf("Fabla2:uiMessage bank %i, pad %i, layer %i: %f\n", b, p, l, v );

	/*
	if( URI == uris->fabla2_PadPlay )
	{
	  printf("DSP has note on from UI: %i, %i, %i\n", b, p, l);
	}
	*/
	Pad* pad = library->bank( b )->pad( p );
	Sample* s = pad->layer( l );
	if( !s ) {
		// abuse the error handling in UI to blank the sample view of UI
		//printf("%s : sample not valid! Fix this.\n", __PRETTY_FUNCTION__);
		LV2_Atom_Forge_Frame frame;
		lv2_atom_forge_frame_time( &lv2->forge, 0 );
		lv2_atom_forge_object( &lv2->forge, &frame, 0, uris->fabla2_ReplyUiSampleState );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_bank);
		lv2_atom_forge_int(&lv2->forge, b );
		lv2_atom_forge_key(&lv2->forge, uris->fabla2_pad);
		lv2_atom_forge_int(&lv2->forge, p );
		lv2_atom_forge_pop(&lv2->forge, &frame);
		return;
	}

	tx_waveform( b, p, l, s->getWaveform() );

	if( URI == uris->fabla2_SampleUnload ) {
		// remove a sample from the engine
		//printf("Fabla2-DSP *Deleteing* sample %s now!\n", s->getName() );

		// tell all voices / pads / samplers that the sample is gone
		for(int i = 0; i < voices.size(); ++i) {
			voices.at(i)->stopIfSample( s );
		}
		pad->remove( s );
		pad->checkAll();

		padRefreshLayers( b, p );
		writePadsState( b, p, pad );

		// TODO - refactor away yasper<ptr> stuff, to manually de-alloc
		//delete s;
	} else if(       URI == uris->fabla2_Panic ) {
		panic();
	} else if(       URI == uris->fabla2_PadVolume ) {
		pad->volume = v;
		writePadsState( b, p, pad );
	} else if(       URI == uris->fabla2_PadAuxBus1 ) {
		pad->sends[0] = v;
	} else if(       URI == uris->fabla2_PadAuxBus2 ) {
		pad->sends[1] = v;
	} else if(       URI == uris->fabla2_PadAuxBus3 ) {
		pad->sends[2] = v;
	} else if(       URI == uris->fabla2_PadAuxBus4 ) {
		pad->sends[3] = v;
	} else if(       URI == uris->fabla2_SamplePitch ) {
		s->dirty = 1;
		s->pitch = v;
	} else if(  URI == uris->fabla2_SampleGain ) {
		//printf("setting gain to %f\n", v );
		s->dirty = 1;
		s->gain = v;
	} else if(  URI == uris->fabla2_SamplePan ) {
		//printf("setting pan to %f\n", v );
		s->dirty = 1;
		s->pan = v;
	} else if(  URI == uris->fabla2_SampleStartPoint ) {
		s->dirty = 1;
		s->startPoint = v;
	} else if(  URI == uris->fabla2_SampleEndPoint ) {
		// TODO FIXME printf("%s: STUB\n", __PRETTY_FUNCTION__);
	} else if(  URI == uris->fabla2_SampleVelStartPnt ) {
		s->dirty = 1;
		s->velocityLow( v );
	} else if(  URI == uris->fabla2_SampleVelEndPnt ) {
		s->dirty = 1;
		s->velocityHigh( v );
	} else if(  URI == uris->fabla2_SampleFilterType ) {
		s->dirty = 1;
		s->filterType = v;
	} else if(  URI == uris->fabla2_SampleFilterFrequency ) {
		s->dirty = 1;
		s->filterFrequency = v;
	} else if(  URI == uris->fabla2_SampleFilterResonance ) {
		s->dirty = 1;
		s->filterResonance = v;
	} else if(  URI == uris->fabla2_SampleAdsrAttack ) {
		s->dirty = 1;
		s->attack = v;
	} else if(  URI == uris->fabla2_SampleAdsrDecay ) {
		s->dirty = 1;
		s->decay = v;
	} else if(  URI == uris->fabla2_SampleAdsrSustain ) {
		s->dirty = 1;
		s->sustain = v;
	} else if(  URI == uris->fabla2_SampleAdsrRelease ) {
		s->dirty = 1;
		s->release = v;
	} else if(  URI == uris->fabla2_PadMuteGroup ) {
		int i = int(v);
		//printf("setting mute group to %d\n", i );
		pad->muteGroup( i );
	} else if(  URI == uris->fabla2_PadOffGroup ) {
		int i = int(v);
		//printf("setting off group to %d\n", i );
		pad->offGroup( i );
	} else if(  URI == uris->fabla2_PadSwitchType ) {
		int c = int(v);
		//printf("pad switch type: %i\n", c );
		if ( c == 0 ) pad->switchSystem( Pad::SS_NONE ); // first sample every time
		if ( c == 1 ) pad->switchSystem( Pad::SS_ROUND_ROBIN ); // iter over all samples
		if ( c == 2 ) pad->switchSystem( Pad::SS_VELOCITY_LAYERS ); // velocity based choice
		if ( c == 3 ) pad->switchSystem( Pad::SS_VELOCITY_VOLUME ); // velocity -> volume
	} else if(  URI == uris->fabla2_PadTriggerMode ) {
		int i = int(v);
		//printf("pad switch type: %d\n", i );
		pad->triggerMode( (Pad::TRIGGER_MODE) i );
	} else if(  URI == uris->fabla2_RequestUiSampleState ) {
		tx_waveform( b, p, l, s->getWaveform() );
		padRefreshLayers( b, p );
		writePadsState( b, p, pad );
		writeSampleState( b, p, l, pad, s );
	}

}

void Fabla2DSP::auxBus( int bus, float value )
{
	auxBusVol[bus] = value;
}

void Fabla2DSP::startRecordToPad( int b, int p )
{
	recordBank  = b;
	recordPad   = p;
	recordIndex = 0;
	recordEnable = true;
	//printf("record starting, bank %i, pad %i\n", recordBank, recordPad );
}

void Fabla2DSP::stopRecordToPad()
{
	//printf("record finished, pad # %i\n", recordPad );

	Pad* pad = library->bank( recordBank )->pad( recordPad );

	printf("%s : NON RT SAFE NEW SAMPLE()\n", __PRETTY_FUNCTION__ );
	Sample* s = new Sample( this, sr, "Recorded", recordIndex, &recordBuffer[0] );

	// reset the pad
	pad->clearAllSamples();
	pad->add( s );

	recordIndex = 0;
	recordEnable = false;
}

Fabla2DSP::~Fabla2DSP()
{
	for(int i = 0; i < voices.size(); i++) {
		delete voices.at(i);
	}
	delete library;
	delete auditionPad;
	delete auditionVoice;
}

}; // Fabla2

