#pragma once

#include <JuceHeader.h>
#include "rnnoise.h"

#define FRAME_SIZE 480
#define MAX_CHANNELS 2

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==============================================================================
    // Your private member variables go here...
    DenoiseState* st = nullptr;

    std::unique_ptr<juce::FileOutputStream> outputFileStream;
    std::unique_ptr<juce::AudioFormatWriter> audioWriter;

    // Recording state
    juce::TextButton recordButton{ "Start Recording" };
    bool isRecording = false;
    bool onStopRecordClicked = false;
    float currentSampleRate = 0.0;

    // Method to start the recording file writer
    void startRecording();
    // Method to stop the recording file writer
    void stopRecording();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
