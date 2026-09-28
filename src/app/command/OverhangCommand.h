#pragma once

#include "command/Command.h"

class QLabel;

namespace cadjitsu {

// Inspect > Overhangs: colours the shown bodies by how printable their
// surfaces are when printed upwards: faint green where fine, amber close to
// the limit, red where a downward face leans further from vertical than the
// limit (it needs support), blue for flat bridges. The build plate is the
// lowest body's bottom; faces resting on it are not coloured. Changes nothing.
class OverhangCommand : public Command {
    Q_OBJECT

public:
    explicit OverhangCommand(const CommandContext &ctx) : Command(ctx, cad::kNoFeature) {}

    QString title() const override { return tr("Overhang Analysis"); }
    QString prompt() const override {
        return tr("Red faces need support when printed upwards; set the limit your printer manages. Enter to close.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &) override { return nullptr; }
    bool makesFeature() const override { return false; }
    bool ready(QString &) override { return true; }
    void showPreview() override;
    void apply() override {}
    void end() override;
    ValueField *canvasValue() const override { return nullptr; }

    ValueField *limitField() const { return m_limit; }
    QLabel *supportLabel() const { return m_support; }

private:
    ValueField *m_limit = nullptr;
    QLabel *m_support = nullptr, *m_bridges = nullptr, *m_near = nullptr, *m_legend = nullptr;
};

} // namespace cadjitsu
