#include "PluginProcessor.h"
#include "PluginEditor.h"

GuiPluginExperimentAudioProcessor::GuiPluginExperimentAudioProcessor()
    : AudioProcessor (BusesProperties())
{
}

void GuiPluginExperimentAudioProcessor::prepareToPlay (double, int)
{
}

void GuiPluginExperimentAudioProcessor::releaseResources()
{
}

bool GuiPluginExperimentAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const
{
    return true;
}

void GuiPluginExperimentAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    buffer.clear();
}

juce::AudioProcessorEditor* GuiPluginExperimentAudioProcessor::createEditor()
{
    return new GuiPluginExperimentAudioProcessorEditor (*this);
}

bool GuiPluginExperimentAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String GuiPluginExperimentAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GuiPluginExperimentAudioProcessor::acceptsMidi() const
{
    return false;
}

bool GuiPluginExperimentAudioProcessor::producesMidi() const
{
    return false;
}

bool GuiPluginExperimentAudioProcessor::isMidiEffect() const
{
    return false;
}

double GuiPluginExperimentAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GuiPluginExperimentAudioProcessor::getNumPrograms()
{
    return 1;
}

int GuiPluginExperimentAudioProcessor::getCurrentProgram()
{
    return 0;
}

void GuiPluginExperimentAudioProcessor::setCurrentProgram (int)
{
}

const juce::String GuiPluginExperimentAudioProcessor::getProgramName (int)
{
    return {};
}

void GuiPluginExperimentAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void GuiPluginExperimentAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
}

void GuiPluginExperimentAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuiPluginExperimentAudioProcessor();
}
