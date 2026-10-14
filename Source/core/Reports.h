#pragma once
#include "Program.h"

namespace xd
{
enum class ReportKind
{
    CurrentValues, SavedValues, DiffWithInit, FullComparison, DifferencesOnly,
    LibraryFullComparison, LibraryDifferences
};
juce::StringArray reportKindNames();

/** Builds a text listing. For the two-program kinds 'a' and 'b' are compared;
    titles name the columns. */
juce::String buildReport (const Program& a, const AddInfo& ia, const juce::String& titleA,
                          const Program* b, const AddInfo* ib, const juce::String& titleB,
                          bool onlyDifferences, const UserNames& names);

/** Morphing between two programs, honouring locked sections. */
Program morph (const Program& from, const Program& to, double amount01, const std::array<bool, numSections>& locked);

/** Global parameters (TABLE 1). The raw unpacked blob is preserved; only known fields are edited. */
struct GlobalField { const char* name; int offset; int minV, maxV; juce::StringArray labels; int dispOffset; };
const std::vector<GlobalField>& globalFields();
juce::String formatGlobal (const GlobalField&, int v);
} // namespace xd
