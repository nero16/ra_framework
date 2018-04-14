#ifndef OX_WRAPPER_DATA_STORE_AGENTS_MATLAB_V4_H
#define OX_WRAPPER_DATA_STORE_AGENTS_MATLAB_V4_H

#include "ox_wrapper/buffer.h"

namespace ox_wrapper { namespace data_store_agents {

class MatlabV4
{
public:
    MatlabV4(std::string const& path, bool append = true);

    ~MatlabV4();

    bool save(OxAbstractBuffer const& source_buffer,
        uint32_t level,
        OxBasicBufferFormat source_buffer_format,
        std::string const& variable_name);

    bool load(OxAbstractBuffer& destination_buffer,
        uint32_t level,
        OxBasicBufferFormat destination_buffer_format,
        std::string const& variable_name);

private:
    std::string m_path;
    bool m_append;
};

}}

#endif
