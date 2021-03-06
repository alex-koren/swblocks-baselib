/*
 * This file is part of the swblocks-baselib library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __BL_APPS_BL_TOOL_COMMANDS_GENERATE_H_
#define __BL_APPS_BL_TOOL_COMMANDS_GENERATE_H_

#include <baselib/core/BaseIncludes.h>

#include <baselib/cmdline/CommandBase.h>

#include <apps/bl-tool/GlobalOptions.h>
#include <apps/bl-tool/commands/GenerateUuids.h>

namespace bltool
{
    namespace commands
    {
        /**
         * @brief class Generate - the command group for generate type commands
         */

        template
        <
            typename E = void
        >
        class GenerateT : public bl::cmdline::CommandBase
        {
            GenerateUuids                 m_generateUuids;

        public:

            GenerateT(
                SAA_inout       bl::cmdline::CommandBase*           parent,
                SAA_in          const GlobalOptions&                globalOptions
                )
                :
                bl::cmdline::CommandBase( parent, "generate" ),
                m_generateUuids( this, globalOptions )
            {
                setHelpMessage(
                    "Usage: @CAPTION@\n"
                    "Supported sub-commands:\n"
                    "@COMMANDS@\n"
                    );
            }
        };

        typedef GenerateT<> Generate;

    } // commands

} // bltool

#endif /* __BL_APPS_BL_TOOL_COMMANDS_GENERATE_H_ */
