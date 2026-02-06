#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

//==============================================================================
/** Generates JUCE controls from a tool's inputSchema + defaultParams.
    Each schema property becomes a labelled control (slider, toggle, combo, or text). */
class SchemaFormComponent : public juce::Component
{
public:
    SchemaFormComponent();
    ~SchemaFormComponent() override;

    /** Clear existing controls and regenerate from schema. */
    void buildFromSchema (const juce::var& inputSchema, const juce::var& defaultParams);

    /** Collect current control values into a DynamicObject. */
    juce::var getParams() const;

    /** Set control values from a var (for test helpers). */
    void setParams (const juce::var& params);

    /** Total pixel height needed to display all fields. */
    int getIdealHeight() const;

    void resized() override;

private:
    struct ParamField;

    void clearFields();
    void addField (const juce::String& name, const juce::var& propSchema, const juce::var& defaultValue);

    std::vector<std::unique_ptr<ParamField>> fields;
    int padding = 6;
    int fieldHeight = 52;
};
