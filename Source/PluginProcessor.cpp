/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "HarmonizerSound.h"
#include "HarmonizerSynthesiserVoice.h"
#include "PluginEditor.h"
#include <aubio/cvec.h>

//==============================================================================
HarmonizerjuceAudioProcessor::HarmonizerjuceAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  AudioChannelSet::mono(), true)
                      #endif
                       .withOutput ("Output", AudioChannelSet::mono(), true)
                     #endif
                       )
#endif
{
    for (int i = 0; i < numVoices; ++i) {
        synthesiser.addVoice(new HarmonizerSynthesiserVoice(*this));
    }
    synthesiser.addSound(new HarmonizerSound());
}

HarmonizerjuceAudioProcessor::~HarmonizerjuceAudioProcessor()
{
}

//==============================================================================
const String HarmonizerjuceAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HarmonizerjuceAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool HarmonizerjuceAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool HarmonizerjuceAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double HarmonizerjuceAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HarmonizerjuceAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int HarmonizerjuceAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HarmonizerjuceAudioProcessor::setCurrentProgram (int index)
{
}

const String HarmonizerjuceAudioProcessor::getProgramName (int index)
{
    return {};
}

void HarmonizerjuceAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void HarmonizerjuceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    expectedBufferSize = samplesPerBlock; // TODO get rid of this

    // Must initialize pitchDetector here because this is the soonest we know
    // block size and sample rate.
    // TODO what if block size and sample rate change?
    if (pitchDetector == nullptr) {
        pitchDetector = new PitchDetector(sampleRate);
    }

    if (phaseVocoder == nullptr) {
        phaseVocoder = new PhaseVocoder(sampleRate, samplesPerBlock);
    }
    if (inputFftWindows == nullptr) {
        inputFftWindows = new cvec_t * [phaseVocoder->getWindowCount()];
        for (int i = 0; i < phaseVocoder->getWindowCount(); ++i) {
            inputFftWindows[i] = new_cvec(phaseVocoder->getWindowSize());
        }
    }
    if (outputFftWindows == nullptr) {
        outputFftWindows = new cvec_t * [phaseVocoder->getWindowCount()];
        for (int i = 0; i < phaseVocoder->getWindowCount(); ++i) {
            outputFftWindows[i] = new_cvec(phaseVocoder->getWindowSize());
        }
    }

    for (int i = 0; i < synthesiser.getNumVoices(); ++i) {
        static_cast<HarmonizerSynthesiserVoice*>(synthesiser.getVoice(i))
            ->prepareToPlay(sampleRate,
                            samplesPerBlock, 
                            phaseVocoder->getWindowSize(),
                            phaseVocoder->getWindowCount(),
                            phaseVocoder->getHopSize());
    }
    synthesiser.setCurrentPlaybackSampleRate(sampleRate);
}

void HarmonizerjuceAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

    // TODO free fft windows here
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool HarmonizerjuceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void HarmonizerjuceAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);

        if (channel == 0) {
            // ..do something to the data...
            pitchDetector->doPitchDetection(channelData, buffer.getNumSamples());
            currentPitch = pitchDetector->getCurrentPitch();
            inputBuffer = channelData;
            inputBufferSize = buffer.getNumSamples();

            // Clear output fft windows
            for (int window = 0; window < phaseVocoder->getWindowCount(); ++window) {
                for (int bin = 0; bin < phaseVocoder->getWindowSize() / 2 + 1; ++bin) {
                    cvec_norm_set_sample(outputFftWindows[window], 0, bin);
                    cvec_phas_set_sample(outputFftWindows[window], 0, bin);
                }
            }

            // Forward transform
            phaseVocoder->doForward(channelData, inputFftWindows, buffer.getNumSamples());

            // Clear output signal
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                channelData[i] = 0;
            }

            // The synthesizer will fill outputFftWindows
            synthesiser.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

            // Reverse transform
            phaseVocoder->doReverse(outputFftWindows, channelData, buffer.getNumSamples());
        } else {
            // Clear output signal
            //for (int i = 0; i < buffer.getNumSamples(); ++i) {
                //channelData[i] = 0;
            //}
        }
    }
}

//==============================================================================
bool HarmonizerjuceAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* HarmonizerjuceAudioProcessor::createEditor()
{
    return new HarmonizerjuceAudioProcessorEditor (*this);
}

//==============================================================================
void HarmonizerjuceAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void HarmonizerjuceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerjuceAudioProcessor();
}
