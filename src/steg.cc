/* Copyright 2011 SRI International
 * See LICENSE for other credits and copying information
 */
#include <vector>
#include <event2/buffer.h>

#include <yaml-cpp/yaml.h>

#include "util.h"
#include "steg.h"

/* Report whether a named steg-module is supported. */

int
steg_is_supported(const char *name)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if (!strcmp(name, (**s).name))
      return 1;
  return 0;
}

/**
   Instantiate a steg module by name and send its confugration defined
   as cmd args. 
*/
steg_config_t *
steg_new(const char *name, config_t *cfg, const std::vector<std::string>& options)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if (!strcmp(name, (**s).name))
      return (**s).new_(cfg, options);
  return 0;
}

/**
   Instantiate a steg module by name and send its confugration defined in 
   a yaml node. 
*/
steg_config_t *
steg_new(const char *name, config_t *cfg, const YAML::Node& options)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if (!strcmp(name, (**s).name))
      return (**s).new_from_yaml_(cfg, options);
  return 0;
}

/* defining the constructor here, so we don't need 
   to include buffer.h to all module who uses steg */
steg_config_t::steg_config_t(config_t* c)
  : cfg(c)
 {
    log_assert(protocol_data_in = evbuffer_new());
    log_assert(protocol_data_out = evbuffer_new());
    
  }

/* Define these here rather than in the class definition so that the
   vtables will be emitted in only one place. */
steg_config_t::~steg_config_t() {}
steg_t::~steg_t() {}
