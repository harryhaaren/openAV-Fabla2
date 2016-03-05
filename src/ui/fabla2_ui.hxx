
#ifndef OPENAV_AVTK_TEST_UI_HXX
#define OPENAV_AVTK_TEST_UI_HXX

#include "avtk.hxx"
#include "step.hxx"
#include "../shared.hxx"

#define OSCPKT_OSTREAM_OUTPUT
#include "oscpkt/oscpkt.hh"
#include "oscpkt/udp.hh"
using namespace oscpkt;

// for write_function and controller
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"


namespace Avtk
{
class Pad;
class Step;
class Fader;
class Widget;
class MixStrip;
};

#define UI_ATOM_BUF_SIZE 128*128
// Sequencer view controls
#define SEQ_ROWS 16
#define SEQ_STEPS 32

class Fabla2UI : public Avtk::UI
{
public:
	/// Set a NativeWindow for embedding: ignore for standalone
	Fabla2UI(PuglNativeWindow parent = 0);

	/// init function, called by LV2 UI wrapper after setting map, forge etc
	void init()
	{
		setBank( 0 );
		currentLayer = 1; // invalidate, so request updates
		requestSampleState( 0, 0, 0 );
		blankSampleState();
	}
	
	void handleMaschine();
	void updateMaschine(int pad, int r, int g, int b, int a);


	void blankSampleState();

	/// widget value callback
	void widgetValueCB( Avtk::Widget* widget);
	/// step-sequencer step button callback
	void seqStepValueCB( Avtk::Widget* w);

	/// handle() fucntion for keybindings
	int handle( const PuglEvent* event );

	/// A revision so the UI can drop update messages when not needed
	int redrawRev;
	int redrawRevDone;

	// left: always visible widgets
	Avtk::Widget* bankBtns[4];

	Avtk::Button* panicButton;

	Avtk::List*  uiViewGroup;
	Avtk::ListItem* liveView;
	Avtk::ListItem* padsView;
	Avtk::ListItem* fileView;
	Avtk::ListItem* seqView;

	Avtk::Widget* followPadBtn;
	Avtk::Widget* recordOverPad;

	Avtk::Dial* masterPitch; // not shown

	// Right
	Avtk::Fader*  masterDB;
	Avtk::Fader*  masterVolume;
	Avtk::Fader*  masterAuxFader1;
	Avtk::Fader*  masterAuxFader2;
	Avtk::Fader*  masterAuxFader3;
	Avtk::Fader*  masterAuxFader4;

	// sample info
	Avtk::Text* sampleName;

	// delete layer dialog
	Avtk::Dialog* deleteLayer;

	// sample edit view
	Avtk::Number* muteGroup;
	Avtk::Number* offGroup;
	Avtk::Number* triggerMode;
	Avtk::Number* switchType;
	Avtk::List* layers;
	Avtk::Widget* adsr;

	Avtk::Widget* filt1;
	Avtk::Number* filterType;
	Avtk::Widget* filterFrequency;
	Avtk::Widget* filterResonance;

	Avtk::Widget* filt2;
	Avtk::Widget* bitcrusDist;
	Avtk::Widget* eq;
	Avtk::Widget* comp;
	Avtk::Widget* gainPitch;
	Avtk::Dial* sampleGain;
	Avtk::Dial* samplePan;
	Avtk::Dial* samplePitch;
	Avtk::Dial* sampleTime;
	Avtk::Dial* sampleStartPoint;
	Avtk::Dial* sampleEndPoint;

	Avtk::Dial* velocityStartPoint;
	Avtk::Dial* velocityEndPoint;

	Avtk::Dial* send1;
	Avtk::Dial* send2;
	Avtk::Dial* send3;
	Avtk::Dial* send4;

	Avtk::Dial* adsrA;
	Avtk::Dial* adsrD;
	Avtk::Dial* adsrS;
	Avtk::Dial* adsrR;

	Avtk::Button* padPlay;
	Avtk::Button* padMute;
	Avtk::Fader* padVolume;

	Avtk::Widget* padSends;
	Avtk::Widget* padMaster;

	// Sample / File loading screen
	Avtk::Box*    sampleViewHeader;
	Avtk::Scroll* sampleDirScroll;
	Avtk::List*   listSampleDirs;
	Avtk::Scroll* sampleFileScroll;
	Avtk::List*   listSampleFiles;
	Avtk::Button* fileViewHome;
	Avtk::Button* fileViewUp;

	// Live view
	Avtk::Group* liveGroup;
	Avtk::Widget* padsHeaderBox;
	
	// Sequencer view
	Avtk::Group*  seqGroup;
	Avtk::Step*   seqSteps[SEQ_ROWS*SEQ_STEPS];
	Avtk::Dial*   transport_bpm;
	Avtk::Button* transport_play;

	// pad - tracks
	Avtk::MixStrip* mixStrip [16];
	Avtk::Fader*    padFaders[16];
	Avtk::Dial*     auxDials [16*4];

	// AuxBus tracks
	Avtk::MixStrip* auxbus[4];
	Avtk::Fader*    auxFaders[4];


	// shared between views!
	/// holds all waveform related widgets: sample name, sample duration etc
	Avtk::Group*    waveformGroup;
	Avtk::Waveform* waveform;

	Avtk::Group*    sampleBrowseGroup;
	Avtk::Group*    sampleControlGroup;

	void padEvent( int bank, int pad, int layer, bool noteOn, int velocity );

	Avtk::Group* padsGroup;
	Avtk::Pad* pads[16];

	// bank/pad/layer currently shown in UI
	int currentBank;
	int currentPad;
	int currentLayer;

	// LV2 ports
	LV2UI_Controller controller;
	LV2UI_Write_Function write_function;

	// LV2 Atom
	URIs uris;
	LV2_URID_Map* map;
	LV2_Atom_Forge forge;

private:
	// OSC interface for Maschine interfaces
	UdpSocket sock;
	std::string maschine_addr;

	/// default directories / file loading
	std::string defaultDir;
	std::string currentDir;
	std::string currentFilesDir;

	/// holds the stripped start of the filename, as presented in List. To build
	/// the loadable /path/filename, we do << currentDir << strippedFilenameStart;
	std::string strippedFilenameStart;

	/// followPad allows the UI to update to the last played PAD.
	bool followPad;

	/// shows the sample browser window instead of the pads
	void showPadsView();
	void showLiveView();
	void showFileView();
	void showSeqView();

	/// updates the UI to a specifc bank
	void setBank( int bank );
	/// write a value to an AuxBus
	void writeAuxBus( int uri, int bus, float value );
	/// writes event/value identified by eventURI using currentBank / currentPad
	void writeAtom( int eventURI, float value );
	/// writes "live view" atoms for a specific pad
	void writeAtomForPad( int eventURI, int pad, float value );
	/// writes a pad play/stop event
	void writePadPlayStop( bool noteOn, int bank, int pad, int layer );
	/// request the state of a sample from the DSP, to show in the UI
	void requestSampleState( int bank, int pad, int layer );
	/// list sample dirs
	void loadNewDir( std::string newDir );
};


#endif // OPENAV_AVTK_TEST_UI_HXX
