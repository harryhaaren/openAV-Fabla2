
#include "fabla2_ui.hxx"

#include "utils.hxx"
#include "theme.hxx"

#include "pad.hxx"
#include "fader.hxx"
#include "volume.hxx"
#include "mixstrip.hxx"

// include the data files for the header images
#include "header_fabla.c"
#include "header_openav.c"

#include <sstream>

#include "../shared.hxx"
#include "../lv2_messaging.hxx"

// hack to get access to PUGL types
// perhaps include AVTK wrapper in future?
#include "avtk/avtk/pugl/event.h"

// implementation of LV2 Atom writing
#include "helper.hxx"

// for file browsing
extern "C" {
#include "sofd/libsofd.h"
}

// AVTK themes
#include "themes.hxx"

// The default OSC port used by Maschine.rs userspace driver
#define PORT_NUM 42435

static void fabla2_ui_seqStepValueCB( Avtk::Widget* w, void* ud )
{
	((Fabla2UI*)ud)->seqStepValueCB(w);
}

enum FABLA2_THEMES {
	THEME_BLUE = 0,
	THEME_ORANGE,
	THEME_GREEN,
	THEME_YELLOW,
	THEME_BLUE_2,
	THEME_RED,
};

Fabla2UI::Fabla2UI( PuglNativeWindow parent ):
	Avtk::UI( 856, 322, parent ),
	currentBank( 0 ),
	currentPad( 0 ),
	currentLayer(0),
	followPad( true )
{
	themes.push_back( new Avtk::Theme( this, AVTK_ORANGE ) );
	themes.push_back( new Avtk::Theme( this, AVTK_GREEN ) );
	themes.push_back( new Avtk::Theme( this, AVTK_YELLOW ) );
	themes.push_back( new Avtk::Theme( this, AVTK_BLUE ) );
	themes.push_back( new Avtk::Theme( this, AVTK_RED ) );
	themes.push_back( new Avtk::Theme( this, AVTK_WHITE ) );

	Avtk::Image* headerImage = 0;
	headerImage = new Avtk::Image( this, 0, 0, 200, 36, "Header Image - Fabla" );
	headerImage->load( header_fabla.pixel_data );
	headerImage = new Avtk::Image( this, w()-130, 0, 130, 36, "Header Image - OpenAV" );
	headerImage->load( header_openav.pixel_data );

	int s = 32;
	bankBtns[0] = new Avtk::Button( this, 5      , 43    , s, s, "A" );
	bankBtns[1] = new Avtk::Button( this, 5 + s+6, 43    , s, s, "B" );
	bankBtns[1]->theme( theme( THEME_ORANGE ) );
	bankBtns[2] = new Avtk::Button( this, 5      , 43+s+6, s, s, "C" );
	bankBtns[2]->theme( theme( THEME_GREEN ) );
	bankBtns[3] = new Avtk::Button( this, 5 + s+6, 43+s+6, s, s, "D" );
	bankBtns[3]->theme( theme( THEME_YELLOW ) );

	for(int i = 0; i < 4; i++)
		bankBtns[i]->clickMode( Avtk::Widget::CLICK_TOGGLE );


	int wx = 5;
	int wy = 43+(s+6)*2; // bottom of bank buttons
	Avtk::Widget* waste = 0;

	panicButton = new Avtk::Button( this, wx, wy, s * 2 + 6, 32,  "PANIC" );
	panicButton->clickMode( Avtk::Widget::CLICK_MOMENTARY );
	panicButton->theme( theme(THEME_RED) );
	wy += 32 + 10;

	waste = new Avtk::Box( this, wx, wy, 70, 74,  "Views" );
	waste->clickMode( Widget::CLICK_NONE );
	waste->value(0);
	wy += 20;

	uiViewGroup = new Avtk::List( this, wx+2, wy, 70-4, 78, "UiViewGroup");
	uiViewGroup->spacing( 2 );
	uiViewGroup->mode      ( Group::WIDTH_EQUAL );
	uiViewGroup->valueMode ( Group::VALUE_SINGLE_CHILD );

	padsView = new Avtk::ListItem( this, wx, 10, 70, 16,  "Pads" );
	padsView->clickMode( Avtk::Widget::CLICK_TOGGLE );
	padsView->value( 1 );

	liveView = new Avtk::ListItem( this, wx, 10, 70, 16,  "Live" );
	liveView->clickMode( Avtk::Widget::CLICK_TOGGLE );

	fileView = new Avtk::ListItem( this, wx, 10, 70, 16,  "File" );
	fileView->clickMode( Avtk::Widget::CLICK_TOGGLE );

	seqView = new Avtk::ListItem( this, wx, 10, 70, 16,  "Seq" );
	seqView->clickMode( Avtk::Widget::CLICK_TOGGLE );

	uiViewGroup->end();
	wy += 78;

	followPadBtn = new Avtk::Button( this, wx, wy, 70, 20,  "Follow" );
	followPadBtn->clickMode( Avtk::Widget::CLICK_TOGGLE );
	wy += 26;

	recordOverPad = new Avtk::Button( this, wx, wy, 70, 30,  "REC" );
	recordOverPad->theme( theme(THEME_RED) );
	recordOverPad->clickMode( Avtk::Widget::CLICK_TOGGLE );


	waveformGroup = new Avtk::Group( this, 355, 42, FABLA2_UI_WAVEFORM_PX, 113, "WaveformGroup");
	waveform = new Avtk::Waveform( this, 355, 42, FABLA2_UI_WAVEFORM_PX, 113, "Waveform" );

	/// waveform overlays
	int waveX = 355;
	int waveY = 42;
	int waveTW = 120;
	sampleName = new Avtk::Text( this, waveX + 8, waveY + 100, waveTW, 14, "-" );

	waveformGroup->end();



	/// sample edit view =========================================================
	int colWidth = 90;
	const int spacer = 4;
	wx = 355;
	wy = 161;
	int divider = 35;
	sampleControlGroup = new Avtk::Group( this, wx, wy, FABLA2_UI_WAVEFORM_PX, 260, "SampleControlGroup");

	/// layers
	waste = new Avtk::Box( this, wx, wy, colWidth, 146,  "Layers" );
	waste->clickMode( Widget::CLICK_NONE );
	layers    = new Avtk::List( this, wx, wy+18, colWidth, 204-18, "LayersList" );
	layers->end();

	/// next column
	wx += colWidth + spacer;
	wy = 161;

	/// Sample triggering options
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Mt-Of-Trg-Swt" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	muteGroup = new Avtk::Number( this, wx + 2, wy + 8, 20, 19, "Mute Group" );
	muteGroup->valueMode( Avtk::Widget::VALUE_INT, 0, 8 );

	offGroup = new Avtk::Number( this, wx + 24, wy + 8, 20, 19, "Off Group" );
	offGroup->valueMode( Avtk::Widget::VALUE_INT, 0, 8 );

	triggerMode = new Avtk::Number( this, wx + 46, wy + 8, 20, 19, "Trigger Mode" );
	triggerMode->valueMode( Avtk::Widget::VALUE_INT, 0, 1 );

	switchType = new Avtk::Number( this, wx + 68, wy + 8, 20, 19, "Switch Type" );
	switchType->valueMode( Avtk::Widget::VALUE_INT, 0, 3 );
	wy += 40;


	/// velocity ranges
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Velocity Map" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	velocityStartPoint = new Avtk::Dial( this, wx     , wy, 40, 40, "Velocity Low" );
	velocityStartPoint->label_visible = 0;
	velocityEndPoint   = new Avtk::Dial( this, wx + 44, wy, 40, 40, "Velocity High" );
	velocityStartPoint->value( 0 );
	velocityEndPoint  ->value( 1 );
	velocityEndPoint->label_visible = 0;
	wy += 40;

	/*
	/// Velocity -> Volume / Filter
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Vel -> Vol-Fil" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	waste = new Avtk::Dial( this, wx     , wy, 40, 40, "VelocityToVolume" );
	waste = new Avtk::Dial( this, wx + 44, wy, 40, 40, "VelocityToFilter" );
	waste->value( 0 );
	wy += 40;
	*/

	/// Filter
	waste = new Avtk::Box( this, wx, wy, colWidth, 50, "Filter" );
	waste->clickMode( Widget::CLICK_NONE );

	filterType = new Avtk::Number( this, wx + colWidth-divider, wy, divider, 14, "Filter Type" );
	filterType->valueMode( Widget::VALUE_INT, 0, 3 );

	wy += 14;
	filterFrequency = new Avtk::Dial( this, wx, wy, 40, 40, "Filter Frequency" );
	filterFrequency->label_visible = 0;
	filterResonance = new Avtk::Dial( this, wx + divider + 10, wy, 40, 40, "Filter Resonance" );
	filterResonance->label_visible = 0;



	/// next col
	wx += colWidth + spacer;
	wy = 161;

	/// gain pan
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Gain / Pan" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 16;
	sampleGain = new Avtk::Dial( this, wx + 4, wy, 40, 40, "Gain" );
	sampleGain->label_visible = false;
	sampleGain->value( 0.75 );
	samplePan  = new Avtk::Dial( this, wx + 46, wy, 40, 40, "Pan" );
	samplePan->value( 0.5 );
	samplePan->label_visible = false;
	wy += 38;

	/// start / end point dials
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Start / End" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	sampleStartPoint = new Avtk::Dial( this, wx     , wy, 40, 40, "Sample Start Point" );
	sampleStartPoint->label_visible = false;
	sampleEndPoint   = new Avtk::Dial( this, wx + 44, wy, 40, 40, "Sample End Point" );
	sampleEndPoint->label_visible = false;
	sampleEndPoint->value( true );
	wy += 40;

	/// pitch / time
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "Pitch / Time" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	samplePitch = new Avtk::Dial( this, wx     , wy, 40, 40, "Pitch" );
	samplePitch->label_visible = false;
	samplePitch->value( 0.5 );
	sampleTime   = new Avtk::Dial( this, wx + 44, wy, 40, 40, "Time" );
	sampleTime->label_visible = false;
	sampleTime->clickMode( Widget::CLICK_NONE );
	sampleTime->theme( theme(THEME_GREEN) );

	/// next col
	wx += colWidth + spacer;
	wy = 161;
	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "ADSR" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 12;
	adsrA = new Avtk::Dial( this, wx - 2 , wy   , 32, 32, "adsrA" );
	adsrD = new Avtk::Dial( this, wx + 21, wy+10, 32, 32, "adsrD" );
	adsrS = new Avtk::Dial( this, wx + 42, wy   , 32, 32, "adsrS" );
	adsrR = new Avtk::Dial( this, wx + 63, wy+10, 32, 32, "adsrR" );
	adsrA->label_visible = false;
	adsrD->label_visible = false;
	adsrS->label_visible = false;
	adsrR->label_visible = false;
	adsrS->value( 1.0 );
	wy += 40;

	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "AuxBus 1 2" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	send1 = new Avtk::Dial( this, wx     , wy, 40, 40, "AuxBus 1" );
	send1->theme( theme(THEME_ORANGE) );
	send1->label_visible = false;
	send2 = new Avtk::Dial( this, wx + 44, wy, 40, 40, "AuxBus 2" );
	send2->theme( theme(THEME_GREEN) );
	send2->label_visible = false;
	wy += 40;

	waste = new Avtk::Box( this, wx, wy, colWidth, 50,  "AuxBus 3 4" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;
	send3 = new Avtk::Dial( this, wx     , wy, 40, 40, "AuxBus 3" );
	send3->label_visible = false;
	send3->theme( theme(THEME_YELLOW) );
	send4 = new Avtk::Dial( this, wx + 44, wy, 40, 40, "AuxBus 4" );
	send4->theme( theme(THEME_RED) );
	send4->label_visible = false;
	wy += 40;

	///  pad master
	wx += colWidth + spacer;
	wy = 161;
	waste = new Avtk::Box( this, wx, wy, colWidth/2, (14+40)*3-4,  "Pad" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 18;
	padPlay = new Avtk::Button( this, wx+3, wy, 38, 15,  "Play" );
	padPlay->theme( theme( THEME_GREEN ) );
	wy += 19;
	padMute = new Avtk::Button( this, wx+3, wy, 38, 15,  "Mute" );
	padMute->clickMode( Widget::CLICK_TOGGLE );
	padMute->theme( theme( THEME_ORANGE ) );
	wy += 55-36;
	padVolume = new Avtk::Fader( this, wx+10, wy, 25, 100,  "PadVolume" );
	padVolume->clickMode( Widget::CLICK_NONE );
	padVolume->value( 0.75 );

	//sampleControlGroup->visible( false );
	sampleControlGroup->end();


	/// load defaults config dir
	loadConfigFile( defaultDir );
	currentDir = defaultDir;

	/// Sample Browser panes =====================================================
	wx = 82;
	wy = 43;
	sampleBrowseGroup = new Avtk::Group( this, wx, wy, 266, 276, "SampleBrowseGroup");
	sampleViewHeader = new Avtk::Box( this, wx, wy, 266, 276,  "Sample Browser" );
	wy += 20 + spacer;

	fileViewHome = new Avtk::Button( this, wx     , wy, 50, 23, "Home" );
	fileViewUp   = new Avtk::Button( this, wx + 55, wy, 50, 23, "Up" );
	wy += 25;

	// samples folder view
	sampleDirScroll = new Avtk::Scroll( this, wx, wy, 110, 166-25, "SampleFilesScroll" );

	listSampleDirs = new Avtk::List( this, 82, 73, 110, 216, "Folder" );
	listSampleDirs->mode      ( Group::WIDTH_EQUAL );
	listSampleDirs->valueMode ( Group::VALUE_SINGLE_CHILD );
	listSampleDirs->resizeMode( Group::RESIZE_FIT_TO_CHILDREN );
	listSampleDirs->end();

	sampleDirScroll->set( listSampleDirs );

	sampleDirScroll->end();

	wx = 198;
	wy = 43 + 20 + spacer;
	// samples view
	sampleFileScroll = new Avtk::Scroll( this, wx, wy, 146, 166, "SampleFilesScroll" );

	listSampleFiles = new Avtk::List( this, 0, 0, 126, 866, "Sample Files" );
	listSampleFiles->mode      ( Group::WIDTH_EQUAL );
	listSampleFiles->valueMode ( Group::VALUE_SINGLE_CHILD );
	listSampleFiles->resizeMode( Group::RESIZE_FIT_TO_CHILDREN );
	listSampleFiles->end();

	sampleFileScroll->set( listSampleFiles );
	sampleFileScroll->end();


	sampleBrowseGroup->visible(false);
	sampleBrowseGroup->end();

	// pads
	wx = 82;
	wy = 43;
	int xS = 57;
	int yS = 56;
	int border = 9;

	padsGroup = new Avtk::Group( this,  wx, wy, 266, 276, "Pads Group");
	waste = new Avtk::Box( this, wx, wy, 266, 276, "Pads" );
	waste->clickMode( Widget::CLICK_NONE );

	int x = 87;
	int y = (yS+border) * 4 - 1.5;
	for(int i = 0; i < 16; i++ ) {
		if( i != 0 && i % 4 == 0 ) {
			y -= yS + border;
			x = 87;
		}

		std::stringstream s;
		s << i + 1;
		pads[i] = new Avtk::Pad( this, x, y, xS, yS, s.str().c_str() );

		x += xS + border;
	}

	padsGroup->end();

	/// live view =======================================================
	wx = 82;
	wy = 43;
	liveGroup = new Avtk::Group( this, wx, wy, 266, 276, "SampleBrowseGroup");

	int livePadsX = 464;
	int livePadsY = 276;
	padsHeaderBox = new Avtk::Box( this, wx, wy, livePadsX, 14,  "16 Pads" );
	padsHeaderBox->clickMode( Widget::CLICK_NONE );
	wy += 14;

	for(int i = 0; i < 16; ++i) {
		int mx = wx + (livePadsX/16.f*i);
		int my = wy;
		int mw = (livePadsX/16.f);

		std::stringstream s;
		s << i + 1;
		mixStrip[i] = new Avtk::MixStrip( this, mx, my, mw, livePadsY - 14, s.str().c_str() );
		mixStrip[i]->clickMode( Widget::CLICK_NONE );
		mixStrip[i]->setNum( s.str() );

		// dials
		int size = mw+4;
		mw -= 6;
		auxDials[ 0+i] = new Avtk::Dial( this, mx, my       , size, size, "Aux1" );
		auxDials[16+i] = new Avtk::Dial( this, mx, my + mw  , size, size, "Aux2" );
		auxDials[32+i] = new Avtk::Dial( this, mx, my + mw*2, size, size, "Aux3" );
		auxDials[48+i] = new Avtk::Dial( this, mx, my + mw*3, size, size, "Aux4" );

		auxDials[ 0+i]->theme( theme( THEME_ORANGE ) );
		auxDials[16+i]->theme( theme( THEME_GREEN  ) );
		auxDials[32+i]->theme( theme( THEME_YELLOW ) );
		auxDials[48+i]->theme( theme( THEME_RED    ) );

		auxDials[ 0+i]->label_visible = false;
		auxDials[16+i]->label_visible = false;
		auxDials[32+i]->label_visible = false;
		auxDials[48+i]->label_visible = false;

		my += mw*5;

		// pad faders
		padFaders[i] = new Avtk::Fader( this, mx + 3, my, mw, 120, "Vol" );
	}

	wx = 82;
	wy = 43;
	wx += padsHeaderBox->w() + spacer;

	waste = new Avtk::Box( this, wx, wy, 228, 14, "AuxBus" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 14;

	for(int i = 0; i < 4; ++i) {
		int mx = wx + (228/4.f*i);
		int my = wy;
		int mw = 228 / 4;
		int mh = 276-14;
		std::stringstream s;
		s << i + 1;

		const char* names[] = {
			"Reverb / Delay",
			"Compression",
			"Sidechain Key",
			"Monitor / Rec",
		};

		auxbus[i] = new Avtk::MixStrip( this, mx, my, mw, mh, names[i] );
		auxbus[i]->clickMode( Widget::CLICK_NONE );
		auxbus[i]->setNum( s.str().c_str() );

		// buttons
		int high = 19;
		my += 4;
		waste = new Avtk::Button( this, mx + 4, my, mw - 8, high,  "Solo" );
		waste->clickMode( Widget::CLICK_TOGGLE );
		waste->theme( theme( THEME_ORANGE ) );
		my += high + 4;

		waste = new Avtk::Button( this, mx + 4, my, mw - 8, high,  "Mute" );
		waste->clickMode( Widget::CLICK_TOGGLE );
		waste->theme( theme( THEME_GREEN ) );
		my += high + 4;

		waste = new Avtk::Button( this, mx + 4, my, mw - 8, high,  "Audit" );
		waste->clickMode( Widget::CLICK_TOGGLE );
		waste->theme( theme( THEME_YELLOW ) );
		my += high + 4;

		waste = new Avtk::Button( this, mx + 4, my, mw - 8, high,  "Meta" );
		waste->clickMode( Widget::CLICK_TOGGLE );
		waste->theme( theme( THEME_RED ) );
		my += high + 4;

		// fader
		mw -= 6;
		my += 24;
		auxFaders[i] = new Avtk::Fader( this, mx + 8, my, 30, 140, names[i] );
		auxFaders[i]->theme( theme( THEME_ORANGE+i) ); // hack for theme indexs
		auxFaders[i]->useCustomTheme = true;
	}
	auxFaders[3]->theme( theme( THEME_RED )); // fix last one
	liveGroup->visible( false );
	liveGroup->end();

	/// sequencer view ===============================================
	wx = 82;
	wy = 43;
	seqGroup = new Avtk::Group( this, wx, wy, 266, 276, "SequencerView");

	int stepSize = 276 / (SEQ_ROWS+3);
	waste = new Avtk::Button(this, wx, wy, 58, (stepSize+3)*16, "Pads");

	wx = 82 + 62;
	wy = 43;
	for(int i = 0; i < SEQ_ROWS; i++) {
		for(int j = 0; j < SEQ_STEPS; j++) {
			Avtk::Step* s = new Avtk::Step(this,
						 wx + j * (stepSize+3), wy,
						 stepSize, stepSize, "-");
			s->clickMode( Widget::CLICK_TOGGLE );
			s->row = i;
			s->col = j;
			if( j % 4 == 0) {
				s->theme(ui->theme(3));
			}
			s->callback = fabla2_ui_seqStepValueCB;
			s->callbackUD = this;
			seqSteps[i*SEQ_STEPS+j] = (Avtk::Step*)s;
		}
		wy += stepSize +3;
	}

	wx = 82 + 62 + (stepSize+3)*SEQ_STEPS + 3;

	seqGroup->end();
	seqGroup->visible( false );

	/// Master view on right
	wx = 782;
	wy = 43;
	waste = new Avtk::Box( this, wx, wy, 70, 276, "Master" );
	waste->clickMode( Widget::CLICK_NONE );
	wy += 18;

	masterAuxFader1 = new Avtk::Fader( this, wx+ 1, wy+3, 15, 90, "Master Aux 1" );
	masterAuxFader1->theme( theme(THEME_ORANGE) );
	masterAuxFader1->useCustomTheme = true;

	masterAuxFader2 = new Avtk::Fader( this, wx+19, wy+3, 15, 90, "Master Aux 2" );
	masterAuxFader2->theme( theme(THEME_GREEN) );
	masterAuxFader2->useCustomTheme = true;

	masterAuxFader3 = new Avtk::Fader( this, wx+37, wy+3, 15, 90, "Master Aux 3" );
	masterAuxFader3->theme( theme(THEME_YELLOW) );
	masterAuxFader3->useCustomTheme = true;

	masterAuxFader4 = new Avtk::Fader( this, wx+55, wy+3, 15, 90, "Master Aux 4" );
	masterAuxFader4->theme( theme(THEME_RED) );
	masterAuxFader4->useCustomTheme = true;

	masterDB = new Avtk::Volume( this, wx+4, wy+96,38, 160,  "MasterDB");
	// FIXME : add white theme to AVTK, then update this
	masterDB->theme( theme(THEME_BLUE) );

	masterVolume = new Avtk::Fader( this, wx+4, wy+96,38, 160,  "Master Vol" );
	//masterVolume->clickMode( Widget::CLICK_NONE );
	masterVolume->value( 0.75 );

	// Transport layer (always visible on top)
	wy = 8;
	wx = 170;
	transport_play = new Avtk::Button(this, wx, wy, 60, 28, "Play");
	wx += 62;
	transport_bpm  = new Avtk::Dial(this, wx, wy-6, 34, 34, "BPM");
	transport_play->clickMode( Widget::CLICK_TOGGLE );

	if( 1 ) // hide WIP features in UI
	{
		transport_play->visible(0);
		transport_bpm->visible(0);
		seqGroup->visible(0);
	}

	// initial values
	bankBtns[0]->value( true );
	followPadBtn->value( followPad );

	redrawRev = 0;

	/// created last, so its on top
	deleteLayer = new Avtk::Dialog( this, 0, 0, 320, 120, "Delete Sample?" );

	sock.bindTo(PORT_NUM);
	if(!sock.isOk()) {
		printf("Fabla2UI: Error opening OSC port %d: %s\n",
		       PORT_NUM, &sock.errorMessage()[0]);
		printf("=> Maschine devices OSC interface wont work!\n");
	} else {
		printf("=> Maschine devices OSC interface OK!\n");
	}
	maschine_addr = std::string();
}

void Fabla2UI::handleMaschine()
{
	PacketReader pr;
	if (sock.receiveNextPacket(0 /* timeout, in ms */)) {
		pr.init(sock.packetData(), sock.packetSize());
		oscpkt::Message *msg;
		while (pr.isOk() && (msg = pr.popMessage()) != 0) {
			//handle_message(&sock, msg);
			int press;
			msg->arg().popInt32(press);

			PacketWriter pw;
			Message repl;
			repl.init(&msg->address[0]).pushInt32(press);
			pw.init().addMessage(repl);
			sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());

			// Save the address, then return
			if( strlen(&maschine_addr[0]) == 0)  {
				maschine_addr = sock.remote_addr.asString();
				printf("Maschine address - %s\n", &maschine_addr[0]);
				PacketWriter pw;
				Message repl;
				repl.init("/maschine/midi_note_base").pushInt32(36);
				pw.init().addMessage(repl);
				sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());

				for(int i = 0; i < 16; i++)
					updateMaschine(i, 10, 31, 0xFF, 15);
				break;
			}

			/* Handle press + release events */
			if(strcmp(&msg->address[0],"/maschine/button/rec") == 0) {
				float tmp = press;
				write_function(controller, Fabla2::RECORD_OVER_LAST_PLAYED_PAD, sizeof(float), 0, &tmp);
			} else if(strcmp(&msg->address[0],"/maschine/button/play") == 0) {
				int current = transport_play->value();
				if(press) {
					current = !current;
					float tmp = current;

					transport_play->value(current);
					write_function(controller, Fabla2::TRANSPORT_PLAY, sizeof(float), 0, &tmp);
				} else {
					PacketWriter pw;
					Message repl;
					repl.init("/maschine/button/play").pushInt32(current);
					pw.init().addMessage(repl);
					sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());
					transport_play->value(current);
				}
			}

			if(!press)
				break; // button release
			if(       strcmp(&msg->address[0],"/maschine/button/f1") == 0) {
				showPadsView();
			} else if(strcmp(&msg->address[0],"/maschine/button/f2") == 0) {
				showLiveView();
			} else if(strcmp(&msg->address[0],"/maschine/button/f3") == 0) {
				showFileView();
				showPadsView();
			}
		}
	}
}

void Fabla2UI::updateMaschine(int pad, int r, int g, int b, int a)
{
	PacketWriter pw;
	Message repl;
	// TODO different banks?

	// convert to maschine way of counting pads
	int col = pad % 4;
	int row = 4 - (pad/4);
	int p = (row-1)*4 + col;
	if( p < 16 && p >= 0 )
	{
		uint32_t color = 0;
		color += r << 16;
		color += g << 8;
		color += b;
		if( a < 100 )
			a = 0; // nuke offs to 0
		repl.init("/maschine/pad").pushInt32(p).pushInt32(color).pushFloat(a/255.f);
		pw.init().addMessage(repl);
		sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());
	}
}

void Fabla2UI::blankSampleState()
{
	padVolume       ->value( 0 );

	muteGroup       ->value( 0 );
	offGroup        ->value( 0 );
	triggerMode     ->value( 0 );
	switchType      ->value( 0 );

	sampleGain      ->value( 0 );
	samplePan       ->value( 0 );

	samplePitch     ->value( 0 );
	sampleTime      ->value( 0 );

	sampleStartPoint->value( 0 );
	sampleEndPoint  ->value( 0 );

	velocityStartPoint->value( 0 );
	velocityEndPoint->value( 0 );

	adsrA->value( 0 );
	adsrD->value( 0 );
	adsrS->value( 0 );
	adsrR->value( 0 );

	send1           ->value( 0 );
	send2           ->value( 0 );
	send3           ->value( 0 );
	send4           ->value( 0 );

	filterType      ->value( 0 );
	filterFrequency ->value( 0 );
	filterResonance ->value( 0 );

	sampleName->label("-");

	layers->clear();

	waveform->setStartPoint( 0 );

	std::vector<float> tmp(FABLA2_UI_WAVEFORM_PX);
	for(int i = 0; i < FABLA2_UI_WAVEFORM_PX; ++i)
		tmp[i] = 0.0;
	waveform->show( tmp );

}


void Fabla2UI::loadNewDir( std::string newDir )
{
	printf("loadNewDir() %s\n", newDir.c_str() );
	std::vector< std::string > tmp;
	int error = Avtk::directories( newDir, tmp, true, true );

	if( !error ) {
		// don't navigate into a dir with only . and ..
		if( tmp.size() > 2 ) {
			currentDir = newDir;
			printf("%s, %d : new dir : %s\n", __PRETTY_FUNCTION__, __LINE__, newDir.c_str() );
			listSampleDirs->clear();
			listSampleDirs->show( tmp );
		} else {
			printf("%s , %d : not moving to sub-dir : has no folders to cd into\n", __PRETTY_FUNCTION__, __LINE__ );
		}


		currentFilesDir = newDir;
		tmp.clear();
		listSampleFiles->clear();
		error = Avtk::directoryContents( currentFilesDir, tmp, strippedFilenameStart );
		if( !error ) {
			if( tmp.size() ) {
				listSampleFiles->show( tmp );
				printf("%s , %d : error showing contents of %s\n", __PRETTY_FUNCTION__, __LINE__, currentFilesDir.c_str() );
			} else {
				printf("tmp.size() == 0, not showing\n");
			}
		}
	} else {
		printf("%s , %d :  Error loading dir %s", __PRETTY_FUNCTION__, __LINE__, newDir.c_str() );
		return;
	}
}

void Fabla2UI::showSeqView()
{
	padsGroup         ->visible( false );
	waveformGroup     ->visible( false );
	sampleBrowseGroup ->visible( false );
	sampleControlGroup->visible( false );
	liveGroup         ->visible( false );

	seqGroup          ->visible( true );
	uiViewGroup->value( 3 );
	redraw();
}

void Fabla2UI::showLiveView()
{
	padsGroup         ->visible( false );
	waveformGroup     ->visible( false );
	sampleBrowseGroup ->visible( false );
	sampleControlGroup->visible( false );
	seqGroup          ->visible( false );

	liveGroup         ->visible( true  );
	uiViewGroup->value( 1 );
	redraw();
}

void Fabla2UI::showPadsView()
{
	liveGroup         ->visible( false );
	sampleBrowseGroup ->visible( false );
	seqGroup          ->visible( false );

	padsGroup         ->visible( true );
	waveformGroup     ->visible( true );
	sampleControlGroup->visible( true );

	uiViewGroup->value( 0 );

	// info could be outdated from live view
	requestSampleState( currentBank, currentPad, currentLayer );
	redraw();
}

static void fabla2_file_select_callback(const char *c, void *userdata)
{
	Fabla2UI* self = (Fabla2UI*)userdata;
#define OBJ_BUF_SIZE 1024
	uint8_t obj_buf[OBJ_BUF_SIZE];
	lv2_atom_forge_set_buffer(&self->forge, obj_buf, OBJ_BUF_SIZE);
	// true = audition sample only flag
	LV2_Atom* msg = writeSetFile( &self->forge, &self->uris, -1, -1, c, true);
	self->write_function(self->controller, 0, lv2_atom_total_size(msg), self->uris.atom_eventTransfer, msg);
}

/// taken from SOFD example - thanks x42 for this awesome library!
// TODO: This can probably be "non-modal" to allow UI redraws in BG
// by using x_fib_handle_events() without the loop, and calling it // using a wrapper-widget or else just manually hack it :)
std::string fabla2_showFileBrowser(std::string dir, Fabla2UI* t)
{
	Display* dpy = XOpenDisplay(0);
	if (!dpy) {
		return "";
	}
	//x_fib_cfg_filter_callback (sofd_filter);
	x_fib_configure (1, "Open File");
	x_fib_load_recent ("/tmp/sofd.recent");
	x_fib_show (dpy, 0, 400, 320);
	x_fib_file_changed_cb( fabla2_file_select_callback, (void *)t);

	// stores result to return
	std::string ret;

	while (1) {
		XEvent event;
		while (XPending (dpy) > 0) {
			XNextEvent (dpy, &event);
			if (x_fib_handle_events (dpy, &event)) {
				if (x_fib_status () > 0) {
					char *fn = x_fib_filename ();
					x_fib_add_recent (fn, time (NULL));

					ret = fn;
					free (fn);
				}
			}
		}
		if (x_fib_status ()) {
			break;
		}
		usleep (80000);
	}
	x_fib_close (dpy);

	x_fib_save_recent ("/tmp/sofd.recent");

	x_fib_free_recent ();
	XCloseDisplay (dpy);

	return ret;
}

void Fabla2UI::showFileView()
{
	liveGroup->visible( false );
	padsGroup->visible( false );
	seqGroup ->visible( false );

	// sofd temp replacing in-UI browser
	sampleBrowseGroup->visible( false );

	waveformGroup->visible( true );
	sampleControlGroup->visible( true );

	// TODO/FIXME: We should return and allow the redraw to occur,
	// and register a callback to open SOFD. This would allow the UI
	// to have redrawed a dialog saying "External file browser open",
	// and automatically close that dialog when done with SOFD.
	redraw();

	/*
	loadNewDir( currentDir );
	sampleFileScroll->set( listSampleFiles );
	*/
	//printf("spawming SOFD now!\n");
	std::string chosen = fabla2_showFileBrowser( currentDir, this );

	if( chosen.size() > 0 ) {
		//printf("SOFD returned %s\n", chosen.c_str() );
#define OBJ_BUF_SIZE 1024
		uint8_t obj_buf[OBJ_BUF_SIZE];
		lv2_atom_forge_set_buffer(&forge, obj_buf, OBJ_BUF_SIZE);
		LV2_Atom* msg = writeSetFile( &forge, &uris, currentBank, currentPad, chosen.c_str(), 0);
		write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
		// return to pads view for triggering
	}
	showPadsView();
	uiViewGroup->value( 0 );
	redraw();
}


void Fabla2UI::padEvent( int bank, int pad, int layer, bool noteOn, int velocity )
{
	//printf("pad event %d - note on %d, layer %d\n", pad, noteOn, layer);
	if( pad < 0 || pad >= 16 ) {
		return; // invalid pad number
	}

	// change widget properties
	pads[pad]->value( noteOn );
	mixStrip[pad]->value(noteOn);

	// Sample view, highlight the layer
	layers->value(layer);

	currentBank  = bank;
	currentPad   = pad;
	if(noteOn) // not off contains -1 in layer!
		currentLayer = layer;

	float fin = noteOn ? 255 : 15;
	//printf("sending pad %d : alpha %f tl maschine\n", pad, fin );
	updateMaschine(pad, 10, 31, 0xFF, fin);

	if(followPad) {
		//requestSampleState( currentBank, currentPad, currentLayer );
	}

	redraw();
}


void Fabla2UI::requestSampleState( int bank, int pad, int layer )
{
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);

	// write message
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 0, uris.fabla2_RequestUiSampleState);

	lv2_atom_forge_key(&forge, uris.fabla2_bank);
	lv2_atom_forge_int(&forge, currentBank );

	lv2_atom_forge_key(&forge, uris.fabla2_pad);
	lv2_atom_forge_int(&forge, currentPad );

	lv2_atom_forge_key(&forge, uris.fabla2_layer);
	lv2_atom_forge_int(&forge, currentLayer );

	//printf("UI writes requestSampleState %i, %i, %i\n", currentBank, currentPad, currentLayer );

	lv2_atom_forge_pop(&forge, &frame);

	// send it
	redrawRev++;
	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

void Fabla2UI::setBank( int bank )
{
	bankBtns[currentBank]->value( false );
	currentBank = bank;
	bankBtns[currentBank]->value( true );

	Avtk::Theme* t = theme( bank );
	waveform->theme( t );

	for(int i = 0; i < 16; i++)
		mixStrip[i]->theme( t );

	for(int i = 0; i < 16; i++) {
		pads[i]->theme( t );
	}
}

void Fabla2UI::writePadPlayStop( bool noteOn, int bank, int pad, int layer )
{
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);

	LV2_Atom_Forge_Frame frame;
	int uri = uris.fabla2_PadStop;
	if( noteOn )
		uri = uris.fabla2_PadPlay;

	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 0, uri);

	lv2_atom_forge_key(&forge, uris.fabla2_bank);
	lv2_atom_forge_int(&forge, bank );

	lv2_atom_forge_key(&forge, uris.fabla2_pad);
	lv2_atom_forge_int(&forge, pad );

	lv2_atom_forge_key(&forge, uris.fabla2_layer);
	lv2_atom_forge_int(&forge, layer );

	lv2_atom_forge_pop(&forge, &frame);

	// send it
	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

void Fabla2UI::writeAtomForPad( int eventURI, int pad, float value )
{
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 0, eventURI);

	lv2_atom_forge_key(&forge, uris.fabla2_bank);
	lv2_atom_forge_int(&forge, currentBank );
	lv2_atom_forge_key(&forge, uris.fabla2_pad);
	lv2_atom_forge_int(&forge, pad );
	lv2_atom_forge_key(&forge, uris.fabla2_layer);
	// layer not used in pad message, but needs to be a valid one.
	lv2_atom_forge_int(&forge, 0 );
	lv2_atom_forge_key(&forge, uris.fabla2_value);
	lv2_atom_forge_float(&forge, value );
	lv2_atom_forge_pop(&forge, &frame);

	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

void Fabla2UI::writeAuxBus( int uri, int bus, float value )
{
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 0, uri);

	lv2_atom_forge_key(&forge, uris.fabla2_auxBusNumber);
	lv2_atom_forge_int(&forge, bus );

	lv2_atom_forge_key  (&forge, uris.fabla2_value );
	lv2_atom_forge_float(&forge, value );

	lv2_atom_forge_pop( &forge, &frame );

	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

void Fabla2UI::writeAtom( int eventURI, float value )
{
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);

	//printf("Fabla2:UI writeAtom %i, %f: pad %d, layer %d\n", eventURI, value, currentPad, currentLayer );
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 0, eventURI);

	lv2_atom_forge_key(&forge, uris.fabla2_bank);
	lv2_atom_forge_int(&forge, currentBank );

	lv2_atom_forge_key(&forge, uris.fabla2_pad);
	lv2_atom_forge_int(&forge, currentPad );

	// don't write layer if its a pad play event, do for audition URIs
	//if( eventURI != uris.fabla2_PadPlay && eventURI != uris.fabla2_PadStop ) {
		lv2_atom_forge_key(&forge, uris.fabla2_layer);
		lv2_atom_forge_int(&forge, currentLayer );
	//}

	lv2_atom_forge_key  (&forge, uris.fabla2_value);
	lv2_atom_forge_float(&forge, value );

	lv2_atom_forge_pop(&forge, &frame);

	// send it
	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

int Fabla2UI::handle( const PuglEvent* e )
{
	// handle key presses here, playing pads press/release
	if( e->type == PUGL_KEY_PRESS ||
	    e->type == PUGL_KEY_RELEASE ) {
		int pad = -1;
		switch( e->key.character ) {
		case 'z':
			pad =  0;
			break;
		case 'x':
			pad =  1;
			break;
		case 'c':
			pad =  2;
			break;
		case 'v':
			pad =  3;
			break;
		case 'a':
			pad =  4;
			break;
		case 's':
			pad =  5;
			break;
		case 'd':
			pad =  6;
			break;
		case 'f':
			pad =  7;
			break;
		case 'q':
			pad =  8;
			break;
		case 'w':
			pad =  9;
			break;
		case 'e':
			pad = 10;
			break;
		case 'r':
			pad = 11;
			break;
		case '1':
			pad = 12;
			break;
		case '2':
			pad = 13;
			break;
		case '3':
			pad = 14;
			break;
		case '4':
			pad = 15;
			break;
		}
		if( pad >= 0 ) {
			int uri = e->type == PUGL_KEY_PRESS ? uris.fabla2_PadPlay : uris.fabla2_PadStop;
			currentPad = pad;
			writeAtom( uri, 1 );
			//printf("playing pad %i, uri %i\n", pad, uri );
			//writePadPlayStop( true, currentBank, pad, 0 );
			if( e->type == PUGL_KEY_PRESS )
				updateMaschine(pad, 0, 51, 255, 255);
			else
				updateMaschine(pad, 0, 51, 255, 25);
			return 1; // handled
		}
	}

	return 0;
}

void Fabla2UI::seqStepValueCB(Avtk::Widget* w)
{
	Avtk::Step* s = (Avtk::Step*) w;
	// TODO write Atom here, setting step to value
	uint8_t obj_buf[UI_ATOM_BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, obj_buf, UI_ATOM_BUF_SIZE);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object( &forge, &frame, 3, uris.fabla2_SeqStepState);
	if( !msg )
		printf("message not valid!!\n");

	lv2_atom_forge_key(&forge, uris.fabla2_bank);
	lv2_atom_forge_int(&forge, 0 );
	lv2_atom_forge_key(&forge, uris.fabla2_pad);
	lv2_atom_forge_int(&forge, 15 - s->row );
	lv2_atom_forge_key(&forge, uris.fabla2_step);
	lv2_atom_forge_int(&forge, s->col );
	lv2_atom_forge_key(&forge, uris.fabla2_value);
	int tmp = int(w->value()+0.5);
	lv2_atom_forge_int(&forge, tmp);

	lv2_atom_forge_pop(&forge, &frame);
	write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
}

void Fabla2UI::widgetValueCB( Avtk::Widget* w)
{
	float tmp = w->value();
	//printf("widgetCB : %s, value: %f\n", w->label(), tmp );
	if( w == recordOverPad ) {
		write_function( controller, Fabla2::RECORD_OVER_LAST_PLAYED_PAD, sizeof(float), 0, &tmp );
	}
	/*
	else if( w == masterPitch )
	{
	  float scaleVal = tmp * 24 - 12;
	  write_function( controller, Fabla2::MASTER_PITCH, sizeof(float), 0, &scaleVal );
	}
	*/
	else if( w == layers ) {
		currentLayer = int( layers->value() );
		//printf("%s %d : currentLayer = %d\n", __FILE__, __LINE__, currentLayer);
		if( w->mouseButton() == 3 ) {
			int mx = w->mouseX();
			int my = w->mouseY();
			printf("%i %i\n", mx, my );

			std::stringstream s;
			s << "Delete layer " << layers->selectedString() << "?";

			deleteLayer->run("Delete Layer", s.str().c_str(), Avtk::Dialog::OK_CANCEL, mx, my );
		} else {
			int lay = int( layers->value() );
			//printf("click on layer %i : value() %f\n", lay, tmp );
			if( true ) //;;tmp > 0.4999 )
				writePadPlayStop( true, currentBank, currentPad, lay );
			else
				writePadPlayStop( false, currentBank, currentPad, lay );
		}
	} else if( w == deleteLayer ) {
		// user clicked OK on delete layer dialog
		if( int(tmp) == 1 ) {
			//printf("UI writing sampleUnload\n");
			writeAtom( uris.fabla2_SampleUnload, true );
			requestSampleState( currentBank, currentPad, currentLayer );
		}
	} else if( w == fileViewUp ) {
		std::string newDir;
		std::string current = listSampleDirs->selectedString();
		Avtk::fileUpLevel( current, newDir );
		loadNewDir( newDir );
	} else if( w == fileViewHome ) {
		std::string newDir = getenv("HOME");
		loadNewDir( newDir );
	} else if( w == panicButton ) {
		writeAtom( uris.fabla2_Panic , true );
	} else if( w == followPadBtn ) {
		followPad = int(tmp);
		if( !followPad ) {
			pads[currentPad]->value(0);
		}
	} else if( w == liveView ) {
		showLiveView();
	} else if( w == seqView ) {
		showSeqView();
	} else if( w == padsView ) {
		showPadsView();
	} else if( w == fileView ) {
		showFileView();
	} else if( w == listSampleDirs ) {
		std::string selected = listSampleDirs->selectedString();
		std::stringstream s;
		s << currentDir << "/" << selected;
		// load the new dir
		loadNewDir( s.str().c_str() );
	} else if( w == listSampleFiles ) {
		/*
		// TMP Replaced by SOFD
		std::string selected = listSampleFiles->selectedString();
		std::stringstream s;
		s << currentFilesDir << "/" << strippedFilenameStart << selected;
		printf("UI sending sample load: %s\n", s.str().c_str() );

		#define OBJ_BUF_SIZE 1024
		uint8_t obj_buf[OBJ_BUF_SIZE];
		lv2_atom_forge_set_buffer(&forge, obj_buf, OBJ_BUF_SIZE);
		LV2_Atom* msg = writeSetFile( &forge, &uris, currentBank, currentPad, s.str() );
		write_function(controller, 0, lv2_atom_total_size(msg), uris.atom_eventTransfer, msg);
		*/
	} else if( w == offGroup ) {
		writeAtom( uris.fabla2_PadOffGroup, tmp );
	} else if( w == muteGroup ) {
		writeAtom( uris.fabla2_PadMuteGroup, tmp );
	} else if( w == triggerMode ) {
		writeAtom( uris.fabla2_PadTriggerMode, tmp );
	} else if( w == switchType ) {
		writeAtom( uris.fabla2_PadSwitchType, tmp );
	} else if( w == padVolume ) {
		writeAtom( uris.fabla2_PadVolume, tmp );
	} else if( w == velocityStartPoint ) {
		writeAtom( uris.fabla2_SampleVelStartPnt, tmp );
	} else if( w == velocityEndPoint ) {
		writeAtom( uris.fabla2_SampleVelEndPnt, tmp );
	} else if( w == sampleGain ) {
		writeAtom( uris.fabla2_SampleGain, tmp );
	} else if( w == samplePitch ) {
		writeAtom( uris.fabla2_SamplePitch, tmp );
	} else if( w == samplePan ) {
		writeAtom( uris.fabla2_SamplePan, tmp );
	} else if( w == sampleStartPoint ) {
		waveform->setStartPoint( tmp );
		writeAtom( uris.fabla2_SampleStartPoint, tmp );
	} else if( w == filterType ) {
		writeAtom( uris.fabla2_SampleFilterType, tmp );
	} else if( w == filterFrequency ) {
		writeAtom( uris.fabla2_SampleFilterFrequency, tmp );
	} else if( w == filterResonance ) {
		writeAtom( uris.fabla2_SampleFilterResonance, tmp );
	} else if( w == masterVolume ) {
		write_function( controller, Fabla2::MASTER_VOL, sizeof(float), 0, &tmp );
	} else if( w == padsView ) {
		/*
		followPad = (int)tmp;
		// reset current "followed" pad to normal color
		if( !followPad ) {
			pads[currentPad]->value( 0 );
			pads[currentPad]->theme( theme( currentBank ) );
		}
		*/
	} else if( w == adsrA ) {
		writeAtom( uris.fabla2_SampleAdsrAttack, tmp );
	} else if( w == adsrD ) {
		writeAtom( uris.fabla2_SampleAdsrDecay, tmp );
	} else if( w == adsrS ) {
		writeAtom( uris.fabla2_SampleAdsrSustain, tmp );
	} else if( w == adsrR ) {
		writeAtom( uris.fabla2_SampleAdsrRelease, tmp );
	} else if( w == send1 ) {
		writeAtom( uris.fabla2_PadAuxBus1, tmp );
	} else if( w == send2 ) {
		writeAtom( uris.fabla2_PadAuxBus2, tmp );
	} else if( w == send3 ) {
		writeAtom( uris.fabla2_PadAuxBus3, tmp );
	} else if( w == send4 ) {
		writeAtom( uris.fabla2_PadAuxBus4, tmp );
	} else if( w == masterAuxFader1 ) {
		auxFaders[0]->value( tmp );
		writeAuxBus( uris.fabla2_AuxBus, 0, tmp );
	} else if( w == masterAuxFader2 ) {
		auxFaders[1]->value( tmp );
		writeAuxBus( uris.fabla2_AuxBus, 1, tmp );
	} else if( w == masterAuxFader3 ) {
		auxFaders[2]->value( tmp );
		writeAuxBus( uris.fabla2_AuxBus, 2, tmp );
	} else if( w == masterAuxFader4 ) {
		auxFaders[3]->value( tmp );
		writeAuxBus( uris.fabla2_AuxBus, 3, tmp );
	} else if( w == transport_bpm ) {
		float v = (200*tmp)+40;
		write_function( controller, Fabla2::TRANSPORT_BPM, sizeof(float), 0, &v );
	} else if( w == transport_play ) {
		write_function( controller, Fabla2::TRANSPORT_PLAY, sizeof(float), 0, &tmp );
	} else {
		// check bank buttons
		for(int i = 0; i < 4; i++) {
			if( w == bankBtns[i] ) {
				setBank( i );
				return;
			} else if( w == auxFaders[i] ) {
				if(i == 0 ) {
					masterAuxFader1->value( tmp );
					writeAuxBus( uris.fabla2_AuxBus, 0, tmp );
				} else if(i == 1 ) {
					masterAuxFader2->value( tmp );
					writeAuxBus( uris.fabla2_AuxBus, 1, tmp );
				} else if(i == 2 ) {
					masterAuxFader3->value( tmp );
					writeAuxBus( uris.fabla2_AuxBus, 2, tmp );
				}
				if(i == 3 ) {
					masterAuxFader4->value( tmp );
					writeAuxBus( uris.fabla2_AuxBus, 3, tmp );
				}
				//printf("AuxBus urid %i\n", uris.fabla2_AuxBus );
			}
		}

		for(int i = 0; i < 16; i++) {
			// check the Aux dials in live view
			for(int aux = 0; aux  < 4; ++aux) {
				if( w == auxDials[aux*16+i] ) {
					currentBank = i/16;
					currentPad  = i%16;
					if( aux == 0 )
						writeAtomForPad( uris.fabla2_PadAuxBus1, i, tmp );
					if( aux == 1 )
						writeAtomForPad( uris.fabla2_PadAuxBus2, i, tmp );
					if( aux == 2 )
						writeAtomForPad( uris.fabla2_PadAuxBus3, i, tmp );
					if( aux == 3 )
						writeAtomForPad( uris.fabla2_PadAuxBus4, i, tmp );
				}
			}

			// check padFaders
			if( w == padFaders[i] ) {
				writeAtomForPad( uris.fabla2_PadVolume, i, tmp );
			}

			// check pads
			if( w == pads[i] ) {
				// right mouse, click press event
				if( w->mouseButton() == 3 && tmp ) {
					// load pad
					currentPad = i;
					showFileView();
				} else {
					if( tmp ) {
						currentPad = i;
						//printf("CurrentPad %d, clicked pad %d\n", currentPad, i);
						writeAtom( uris.fabla2_PadPlay, w->value() );
						if(pads[currentPad]->loaded_)
							updateMaschine(i, 10, 31, 255, 255);
							if(followPad) {
								requestSampleState( currentBank, currentPad, currentLayer );
							}
					} else {
						writeAtom( uris.fabla2_PadStop, 0 );
						updateMaschine(i, 10, 31, 255, 10);
					}
				}
				return;
			}
		}
	}
}
