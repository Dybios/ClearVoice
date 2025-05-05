#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // Add the record button and make it visible
    addAndMakeVisible(recordButton);

    // Set up the button action
    recordButton.onClick = [this]() {
        if (isRecording) {
            stopRecording();
            onStopRecordClicked = true;
        }
        else {
            startRecording();
            onStopRecordClicked = false;
        }};

    // Make sure you set the size of the component after
    // you add any child components.
    setSize (800, 600);

    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
    {
        // Specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
    }
}

MainComponent::~MainComponent()
{
    // Stops the recording if it was running
    stopRecording();

    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    // This function will be called when the audio device is started, or when
    // its settings (i.e. sample rate, block size, etc) are changed.

    // You can use this function to initialise any resources you might need,
    // but be careful - it will be called on the audio thread, not the GUI thread.

    // For more details, see the help for AudioProcessor::prepareToPlay()
    
    currentSampleRate = sampleRate;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Your audio-processing code goes here!

    // For more details, see the help for AudioProcessor::getNextAudioBlock()
    
    // TODO: Needs to be wrapped in a lock to let process function finish before stopRecording is called.
    if (isRecording) {
        // Get the number of input and output channels that are actually active.
        auto* device = deviceManager.getCurrentAudioDevice();
        auto activeInputChannels = device->getActiveInputChannels();
        auto activeOutputChannels = device->getActiveOutputChannels(); // Still get this for completeness, but we have 0 outputs requested
        auto numSamples = bufferToFill.numSamples;
        auto startSample = bufferToFill.startSample;

        auto numChannelsForWriter = 0;
        if (audioWriter != nullptr) {
            numChannelsForWriter = audioWriter->getNumChannels();
        }

        // Apply noise cancelling effect
        int currentInputChannelIndex = -1;
        while ((currentInputChannelIndex = activeInputChannels.findNextSetBit(currentInputChannelIndex + 1)) != -1)
        {
            // currentInputChannelIndex is the index of the current active input channel
            if (currentInputChannelIndex >= 0 && currentInputChannelIndex < bufferToFill.buffer->getNumChannels())
            {
                // Get write pointer to modify in place
                float* channelData = bufferToFill.buffer->getWritePointer(currentInputChannelIndex, startSample);

                for (int i = 0; i < numSamples; ++i) {
                    channelData[i] = channelData[i] * 32768.0f;
                }

                rnnoise_process_frame(st, channelData, channelData);

                for (int i = 0; i < numSamples; ++i) {
                    channelData[i] = std::clamp(channelData[i], -32768.0f, 32767.0f) * (1.0f / 32768.0f);
                }

            }
        }

        // --- Writing to File ---
        // If the audio writer is ready and there are channels to write, write the processed input data to the file.
        if (audioWriter != nullptr && audioWriter->getNumChannels() > 0)
        {
            // Create a temporary buffer containing just the processed input channels
            // to ensure we write the correct data to the file.
            juce::AudioBuffer<float> dataToWrite(numChannelsForWriter, numSamples);
            dataToWrite.clear(); // Clear the temporary buffer

            // Copy processed input data to the temporary buffer
            int outputChannelIndex = 0;
            currentInputChannelIndex = -1; // Reset for finding set bits again
            while ((currentInputChannelIndex = activeInputChannels.findNextSetBit(currentInputChannelIndex + 1)) != -1 && outputChannelIndex < numChannelsForWriter)
            {
                if (currentInputChannelIndex >= 0 && currentInputChannelIndex < bufferToFill.buffer->getNumChannels())
                {
                    dataToWrite.copyFrom(outputChannelIndex, 0,
                        bufferToFill.buffer->getReadPointer(currentInputChannelIndex, startSample),
                        numSamples);
                }
                // If an active input channel index is somehow out of bounds for bufferToFill,
                // the corresponding channel in dataToWrite will remain cleared (silent).

                outputChannelIndex++; // Move to the next channel in the dataToWrite buffer
            }

            // Write the temporary buffer to the file.
            bool writeSuccess = audioWriter->writeFromAudioSampleBuffer(dataToWrite, 0, numSamples);

            if (!writeSuccess)
            {
                DBG("Error: audioWriter->writeFromAudioSampleBuffer failed!");
                return;
            }

            // Flush the data to the file to write as it records
            audioWriter->flush();
        }
    }
}

void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // You can add your drawing code here!
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);

    juce::String statusText = "Press the button to start recording.";

    if (isRecording)
    {
        statusText = "Recording microphone input to:\n" + outputFile.getFullPathName();
        g.setColour(juce::Colours::red); // Indicate recording with red text
    }
    else if (audioWriter != nullptr)
    {
        // This state should ideally not be reached if stopRecording cleans up properly
        statusText = "Audio writer is active but not recording?";
        g.setColour(juce::Colours::orange);
    }
    else if (currentSampleRate == 0.0)
    {
        statusText = "Waiting for audio device to start...";
        g.setColour(juce::Colours::yellow);
    }
    else if (!isRecording && onStopRecordClicked)
    {
        statusText = "Stopped recording. File saved to:\n" + outputFile.getFullPathName();
    }

    // Adjust text bounds to make space for the button
    auto textBounds = getLocalBounds();
    textBounds.removeFromBottom(recordButton.getHeight() + 10); // Make space for the button

    g.drawText(statusText, textBounds, juce::Justification::centred, true);
}

void MainComponent::resized()
{
    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.

    // Position the record button
    recordButton.setBounds(getWidth() / 2 - 75, getHeight() - 40, 150, 30);
}

void MainComponent::startRecording()
{
    if (!isRecording) {
        // Create a new rnnoise instance
        st = rnnoise_create(NULL);

        // This will create a file named "processed_audio.wav"
        outputFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("processed_audio.wav");

        if (outputFile.exists()) {
            outputFile.deleteFile();
        }

        outputFileStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());

        if (outputFileStream != nullptr)
        {
            // Get the WAV audio format.
            juce::WavAudioFormat wavFormat;

            // Create an AudioFormatWriter.
            audioWriter.reset(wavFormat.createWriterFor(outputFileStream.get(), // The stream to write to
                currentSampleRate,              // The sample rate
                MAX_CHANNELS,
                24,                      // Bit depth (e.g., 24 bits)
                {},                      // Metadata
                0));                     // Flags

            if (audioWriter != nullptr)
            {
                // Writer is ready
                isRecording = true;
                recordButton.setButtonText("Stop Recording");
                DBG("Audio file writer successfully created for: " + outputFile.getFullPathName());
            }
            else
            {
                DBG("Error: Could not create audio format writer!");
                outputFileStream.reset(); // Close the stream if the writer failed
            }
        }
        else
        {
            DBG("Error: Could not create output file stream for: " + outputFile.getFullPathName());
        }
    }
    repaint();
}

void MainComponent::stopRecording()
{
    if (isRecording)
    {
        isRecording = false;

        // Destroy the rnnoise instance
        rnnoise_destroy(st);

        // Reset audio writer and release output stream
        audioWriter.reset();
        outputFileStream.release();

        recordButton.setButtonText("Start Recording");
    }
    repaint();
}