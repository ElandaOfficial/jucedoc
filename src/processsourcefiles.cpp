
#include "processsourcefiles.h"

void addDoxygenGroup(const juce::File &path, const juce::String &groupName)
{
    const juce::String content = path.loadFileAsString();
    (void) path.deleteFile();
    
    juce::FileOutputStream fos(path);
    fos.writeText("\n/** @weakgroup " + groupName + "\n *  @{\n */\n" + content + "\n" + "/** @}*/\n",
                  false, false, "\n");
}

void processSourceFiles(const juce::File &sourceDir, const juce::File &outputDir)
{
    juce::String module_definitions;
    
    for (const auto &dir : juce::RangedDirectoryIterator(sourceDir, false, "*", juce::File::findDirectories))
    {
        const juce::String module_name   = dir.getFile().getFileName();
        const juce::File   module_path   = outputDir.getChildFile(module_name);
        const juce::File   module_header = module_path.getChildFile(module_name + ".h");
        
        (void) dir.getFile().copyDirectoryTo(module_path);
        
        const juce::String module_def
            = module_header.loadFileAsString().fromFirstOccurrenceOf("BEGIN_JUCE_MODULE_DECLARATION", false, true)
                                              .upToFirstOccurrenceOf("END_JUCE_MODULE_DECLARATION",   false, true);
    
        (void) module_header.deleteFile();
    
        juce::String detail_lines;
        juce::String short_description;
        
        if (!module_def.isEmpty())
        {
            juce::StringArray lines;
            lines.addTokens(module_def, "\n", "");
        
            for (const auto &line : lines)
            {
                if (const juce::String stripped = line.trim(); !stripped.isEmpty())
                {
                    if (line.matchesWildcard("*description:*", true))
                    {
                        short_description = stripped.fromFirstOccurrenceOf("description:", false, true).trim();
                    }
                    else
                    {
                        detail_lines << "    - " << stripped << "\n";
                    }
                }
            }
        }
        
        juce::String module_definiton;
        module_definiton << "/** @defgroup " << module_name << " " << module_name << "\n"
                         << "    " << short_description << "\n\n" << detail_lines
                         << "\n    @{\n*/";
        
        static constexpr int files_and_dirs = juce::File::findFilesAndDirectories;
        
        for (const auto &item : juce::RangedDirectoryIterator(module_path, false, "*", files_and_dirs))
        {
            const juce::File sub_file = item.getFile();
            
            if (sub_file.isDirectory())
            {
                if (sub_file.getFileName() != "native")
                {
                    const juce::String subgroup_name = module_name + "-" + sub_file.getFileName();
                    
                    for (const auto &file : juce::RangedDirectoryIterator(sub_file, true, "juce_*.h;juce_*.dox"))
                    {
                        addDoxygenGroup(file.getFile(), subgroup_name);
                    }
                    
                    module_definiton << "\n" << "/** @defgroup " << subgroup_name << " " << sub_file.getFileName()
                                     << " */\n";
                }
            }
            else
            {
                if (   sub_file.getFileName().matchesWildcard("juce_*.h", true)
                    || sub_file.getFileName().matchesWildcard("juce_*.dx", true))
                {
                    addDoxygenGroup(sub_file, module_name);
                }
            }
        }
        
        module_definitions << "\n" << module_definiton << "\n" << "/** @} */";
    }
    
    juce::FileOutputStream fos(outputDir.getChildFile("juce_modules.dox"));
    
    if (fos.failedToOpen())
    {
        throw std::logic_error("Couldn't create/open file: "
                               + outputDir.getChildFile("juce_modules.dox").getFullPathName().toStdString());
    }
    
    fos.writeText("\n\n" + module_definitions, false, false, "\n");
}
