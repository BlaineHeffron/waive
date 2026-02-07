#include "SchemaFormComponent.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

//==============================================================================
struct SchemaFormComponent::ParamField
{
    juce::String name;
    juce::String type;       // "number", "integer", "boolean", "string"
    bool hasMinMax = false;
    bool hasEnum = false;

    juce::Label label;
    juce::Label caption;

    // Only one of these is visible at a time
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::TextEditor> textEditor;
};

//==============================================================================
SchemaFormComponent::SchemaFormComponent() = default;

SchemaFormComponent::~SchemaFormComponent()
{
    clearFields();
}

void SchemaFormComponent::clearFields()
{
    for (auto& f : fields)
    {
        removeChildComponent (&f->label);
        removeChildComponent (&f->caption);
        if (f->slider)     removeChildComponent (f->slider.get());
        if (f->toggle)     removeChildComponent (f->toggle.get());
        if (f->combo)      removeChildComponent (f->combo.get());
        if (f->textEditor) removeChildComponent (f->textEditor.get());
    }
    fields.clear();
}

void SchemaFormComponent::buildFromSchema (const juce::var& inputSchema, const juce::var& defaultParams)
{
    clearFields();

    auto* propsObj = inputSchema.getProperty ("properties", {}).getDynamicObject();
    if (propsObj == nullptr)
        return;

    for (const auto& prop : propsObj->getProperties())
    {
        auto propName = prop.name.toString();
        auto propSchema = prop.value;

        juce::var defaultValue;
        if (defaultParams.isObject())
            defaultValue = defaultParams.getProperty (propName, {});

        if (defaultValue.isVoid())
            defaultValue = propSchema.getProperty ("default", {});

        addField (propName, propSchema, defaultValue);
    }

    resized();
}

void SchemaFormComponent::addField (const juce::String& name, const juce::var& propSchema, const juce::var& defaultValue)
{
    auto field = std::make_unique<ParamField>();
    field->name = name;

    auto schemaType = propSchema.getProperty ("type", "string").toString();
    field->type = schemaType;

    // Humanize name for label
    auto displayName = name.replace ("_", " ");
    if (displayName.isNotEmpty())
        displayName = displayName.substring (0, 1).toUpperCase() + displayName.substring (1);

    field->label.setText (displayName, juce::dontSendNotification);
    field->label.setJustificationType (juce::Justification::centredLeft);
    field->label.setFont (waive::Fonts::body());
    addAndMakeVisible (field->label);

    auto description = propSchema.getProperty ("description", "").toString();
    if (description.isNotEmpty())
    {
        field->caption.setText (description, juce::dontSendNotification);
        field->caption.setFont (waive::Fonts::caption());
        field->caption.setColour (juce::Label::textColourId, juce::Colours::grey);
        field->caption.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (field->caption);
    }

    auto hasMin = ! propSchema.getProperty ("minimum", {}).isVoid();
    auto hasMax = ! propSchema.getProperty ("maximum", {}).isVoid();
    auto enumArray = propSchema.getProperty ("enum", {});
    field->hasMinMax = hasMin && hasMax;
    field->hasEnum = enumArray.isArray();

    if (schemaType == "boolean")
    {
        field->toggle = std::make_unique<juce::ToggleButton>();
        field->toggle->setToggleState (defaultValue.isVoid() ? false : (bool) defaultValue,
                                       juce::dontSendNotification);
        if (description.isNotEmpty())
            field->toggle->setTooltip (description);
        addAndMakeVisible (field->toggle.get());
    }
    else if ((schemaType == "number" || schemaType == "integer") && field->hasMinMax)
    {
        field->slider = std::make_unique<juce::Slider>();
        field->slider->setSliderStyle (juce::Slider::LinearHorizontal);
        field->slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 18);

        double minVal = (double) propSchema["minimum"];
        double maxVal = (double) propSchema["maximum"];
        double step = schemaType == "integer" ? 1.0 : 0.1;
        field->slider->setRange (minVal, maxVal, step);

        if (! defaultValue.isVoid())
            field->slider->setValue ((double) defaultValue, juce::dontSendNotification);

        if (description.isNotEmpty())
            field->slider->setTooltip (description);
        addAndMakeVisible (field->slider.get());
    }
    else if (schemaType == "string" && field->hasEnum)
    {
        field->combo = std::make_unique<juce::ComboBox>();
        auto* arr = enumArray.getArray();
        if (arr != nullptr)
        {
            int itemId = 1;
            for (const auto& val : *arr)
                field->combo->addItem (val.toString(), itemId++);

            if (! defaultValue.isVoid())
            {
                auto defaultStr = defaultValue.toString();
                for (int i = 0; i < field->combo->getNumItems(); ++i)
                    if (field->combo->getItemText (i) == defaultStr)
                    {
                        field->combo->setSelectedItemIndex (i, juce::dontSendNotification);
                        break;
                    }
            }
            else if (field->combo->getNumItems() > 0)
            {
                field->combo->setSelectedItemIndex (0, juce::dontSendNotification);
            }
        }
        if (description.isNotEmpty())
            field->combo->setTooltip (description);
        addAndMakeVisible (field->combo.get());
    }
    else
    {
        // Fallback: text editor
        field->textEditor = std::make_unique<juce::TextEditor>();
        field->textEditor->setMultiLine (false);

        if (schemaType == "number" || schemaType == "integer")
            field->textEditor->setInputRestrictions (0, "0123456789.-+eE");

        if (! defaultValue.isVoid())
            field->textEditor->setText (defaultValue.toString(), false);

        if (description.isNotEmpty())
            field->textEditor->setTooltip (description);
        addAndMakeVisible (field->textEditor.get());
    }

    fields.push_back (std::move (field));
}

juce::var SchemaFormComponent::getParams() const
{
    auto* obj = new juce::DynamicObject();

    for (const auto& f : fields)
    {
        if (f->toggle)
        {
            obj->setProperty (f->name, f->toggle->getToggleState());
        }
        else if (f->slider)
        {
            if (f->type == "integer")
                obj->setProperty (f->name, (int) f->slider->getValue());
            else
                obj->setProperty (f->name, f->slider->getValue());
        }
        else if (f->combo)
        {
            obj->setProperty (f->name, f->combo->getText());
        }
        else if (f->textEditor)
        {
            auto text = f->textEditor->getText().trim();
            if (f->type == "number")
                obj->setProperty (f->name, text.getDoubleValue());
            else if (f->type == "integer")
                obj->setProperty (f->name, text.getIntValue());
            else
                obj->setProperty (f->name, text);
        }
    }

    return juce::var (obj);
}

void SchemaFormComponent::setParams (const juce::var& params)
{
    if (! params.isObject())
        return;

    for (const auto& f : fields)
    {
        auto val = params.getProperty (f->name, {});
        if (val.isVoid())
            continue;

        if (f->toggle)
            f->toggle->setToggleState ((bool) val, juce::dontSendNotification);
        else if (f->slider)
            f->slider->setValue ((double) val, juce::dontSendNotification);
        else if (f->combo)
        {
            auto text = val.toString();
            for (int i = 0; i < f->combo->getNumItems(); ++i)
                if (f->combo->getItemText (i) == text)
                {
                    f->combo->setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }
        }
        else if (f->textEditor)
            f->textEditor->setText (val.toString(), false);
    }
}

int SchemaFormComponent::getIdealHeight() const
{
    return (int) fields.size() * fieldHeight + waive::Spacing::md;
}

void SchemaFormComponent::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::md, 0);
    int y = waive::Spacing::md;

    for (auto& f : fields)
    {
        auto row = bounds.withY (y).withHeight (fieldHeight);

        auto labelRow = row.removeFromTop (waive::Spacing::lg);
        f->label.setBounds (labelRow);

        auto controlRow = row.removeFromTop (22);

        if (f->toggle)
            f->toggle->setBounds (controlRow);
        else if (f->slider)
            f->slider->setBounds (controlRow);
        else if (f->combo)
            f->combo->setBounds (controlRow);
        else if (f->textEditor)
            f->textEditor->setBounds (controlRow);

        if (f->caption.getText().isNotEmpty())
        {
            auto captionRow = row.removeFromTop (waive::Spacing::md);
            f->caption.setBounds (captionRow);
        }

        y += fieldHeight;
    }
}
