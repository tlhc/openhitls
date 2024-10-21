#!/usr/bin/env python
# -*- coding: utf-8 -*-
# This file is part of the openHiTLS project.
#
# openHiTLS is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#     http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
import sys
sys.dont_write_bytecode = True
import json
import os
import glob
import platform
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__))))
from methods import trans2list, unique_list, save_json_file

class FeatureParser:
    """ Parsing feature files """
    lib_dir_map = {
        "hitls_bsl": "bsl",
        "hitls_crypto": "crypto",
        "hitls_tls": "tls",
        "hitls_x509": "x509"
    }

    def __init__(self, file_path):
        self._fp = file_path
        with open(file_path, 'r', encoding='utf-8') as f:
            self._cfg = json.loads(f.read())
            self._file_check()

        # Features and related information.
        self._feas_info = self._get_feas_info()
        # Assembly type supported by the openHiTLS.
        self._asm_types = self._get_asm_types()

    @property
    def libs(self):
        return self._cfg['libs']

    @property
    def modules(self):
        return self._cfg['modules']

    @property
    def asm_types(self):
        return self._asm_types

    @property
    def feas_info(self):
        return self._feas_info

    def _file_check(self):
        if 'libs' not in self._cfg or 'modules' not in self._cfg:
            raise FileNotFoundError("The format of file %s is incorrect." % self._fp)

    @staticmethod
    def _add_key_value(obj, key, value):
        if value:
            obj[key] = value

    def _add_fea(self, feas_info, fea, lib, parent, children, opts, deps, impl, ins_set):
        feas_info.setdefault(fea, {})
        self._add_key_value(feas_info[fea], 'lib', lib)
        self._add_key_value(feas_info[fea], 'parent', parent)
        self._add_key_value(feas_info[fea], 'children', children)
        self._add_key_value(feas_info[fea], 'opts', opts)
        self._add_key_value(feas_info[fea], 'deps', deps)

        feas_info[fea].setdefault('impl', {})
        feas_info[fea]['impl'][impl] = ins_set if ins_set else []

    def _parse_fea_obj(self, lib, impl, parent, fea, fea_obj, feas_info):
        if not fea_obj:
            self._add_fea(feas_info, fea, lib, parent, None, None, None, impl, None)
            return

        opts = None
        deps = None
        ins_set = None
        children = []
        for key, obj in fea_obj.items():
            if key == 'deps':
                deps = obj
                continue
            if key == 'opts':
                opts = obj
                continue
            if key == 'ins_set':
                ins_set = obj
                continue
            children.append(key)
            self._parse_fea_obj(lib, impl, fea, key, obj, feas_info)

        self._add_fea(feas_info, fea, lib, parent, children, opts, deps, impl, ins_set)

    def _get_feas_info(self):
        """
        description: Parse the feature.json file to obtain feature information
                     and check that feature names in different libraries are unique.
        return:
            feas_info: {
                "children":[],  "parent":[],  "deps":[],
                "opts":[[],[]],  "lib":"",
                "impl":{"c":[], "armv8:[], ...}, # [] lists the instruction sets supported by the feature.
            }
        """
        all_feas = set()
        lib_feas_info = {}
        for lib, lib_obj in self._cfg['libs'].items():
            lib_feas_info[lib] = {}

            for impl, impl_obj in lib_obj['features'].items():
                for fea, fea_obj in impl_obj.items():
                    self._parse_fea_obj(lib, impl, None, fea, fea_obj, lib_feas_info[lib])

            # Check that feature names in different libraries are unique.
            lib_feas = set(lib_feas_info[lib].keys())
            repeat_feas = all_feas.intersection(lib_feas)
            if len(repeat_feas) != 0:
                raise ValueError("Error: feature '%s' has been defined in multiple libs." % (repeat_feas))
            all_feas.update(lib_feas)

        feas_info = {}
        for obj in lib_feas_info.values():
            feas_info.update(obj)
        self._fill_fea_modules(feas_info)
        return feas_info

    def _fill_fea_modules(self, feas_info):
        for top_mod in self.modules:
            for mod, mod_obj in self.modules[top_mod].items():
                formated_mod = "{}::{}".format(top_mod, mod)
                for fea in mod_obj.get('.features', []):
                    if fea not in feas_info:
                        raise ValueError("Unrecognized '%s' in '.features' of '%s::%s'" % (fea, top_mod, mod))
                    if 'modules' not in feas_info[fea]:
                        feas_info[fea]['modules'] = [formated_mod]
                    else:
                        feas_info[fea]['modules'].append(formated_mod)

    def _get_asm_types(self):
        asm_type_set = set()
        [asm_type_set.update(self.libs[lib]['features'].keys()) for lib in self.libs.keys()]
        asm_type_set.discard('c')
        asm_type_set.add('no_asm')
        return asm_type_set

    def get_module_deps(self, module, dep_list, result):
        return self._get_module_deps(module, dep_list, result)

    def _get_module_deps(self, module, dep_list, result):
        """
        Recursively obtains the modules on which the modules depend.
        module:   [IN]  module name, such as crypto::sha2
        dep_list: [OUT] Dependency list, which is an intermediate variable
        result:   [OUT] result
        """
        top_module, sub_module = module.split('::')
        mod_obj = self.modules[top_module][sub_module]

        if '.deps' not in mod_obj:
            result.update(dep_list)
            return

        for dep_mod in mod_obj['.deps']:
            if dep_mod in dep_list:
                # A dependency that already exists in a dependency chain is a circular dependency.
                raise Exception("Cyclic dependency")
            dep_list.append(dep_mod)
            self._get_module_deps(dep_mod, dep_list, result)
            dep_list.pop()

    def get_mod_srcs(self, top_mod, sub_mod, mod_obj):
        srcs = self._cfg['modules'][top_mod][sub_mod]['.srcs']
        asm_type = mod_obj.get('asmType', 'c')
        inc = mod_obj.get('incSet', '')

        blurred_srcs = []
        if not isinstance(srcs, dict):
            blurred_srcs.extend(trans2list(srcs))
            return blurred_srcs

        blurred_srcs.extend(trans2list(srcs.get('public', [])))
        if asm_type == 'c':
            blurred_srcs.extend(trans2list(srcs.get('no_asm', [])))
            return blurred_srcs

        if asm_type not in srcs:
            raise ValueError("Missing '.srcs[%s]' in modules '%s::%s'" % (asm_type, top_mod, sub_mod))
        if not isinstance(srcs[asm_type], dict):
            blurred_srcs.extend(trans2list(srcs[asm_type]))
            return blurred_srcs

        if inc:
            blurred_srcs.extend(trans2list(srcs[asm_type][inc]))
        else:
            first_key = list(srcs[asm_type].keys())[0]
            blurred_srcs.extend(trans2list(srcs[asm_type][first_key]))
        return blurred_srcs

class FeatureConfigParser:
    """ Parses the user feature configuration file. """
    # Specifications of keys and values in the file.
    key_value = {
        "system": {"require": False, "type": str, "choices": ["linux"], "default": "linux"},
        "bits": {"require": False, "type": int, "choices": [32, 64], "default": 64},
        "endian": {"require": True, "type": str, "choices": ["little", "big"], "default": "little"},
        "libType": {
            "require": True,
            "type": list,
            "choices": ["static", "shared", "object"],
            "default": ["static", "shared", "object"]
        },
        "asmType":{"require": True, "type": str, "choices": [], "default": "no_asm"},
        "libs":{"require": True, "type": dict, "choices": [], "default": {}}
    }

    def __init__(self, features: FeatureParser, file_path):
        self._features = features
        self._config_file = file_path
        with open(file_path, 'r', encoding='utf-8') as f:
            self._cfg = json.loads(f.read())
        self.key_value['asmType']['choices'] = list(features.asm_types)
        self.key_value['libs']['choices'] = list(features.libs)
        self._file_check()

    @classmethod
    def default_cfg(cls):
        config = {}
        for key in cls.key_value.keys():
            if cls.key_value[key]["require"]:
                config[key] = cls.key_value[key]["default"]
        return config

    @property
    def libs(self):
        return self._cfg['libs']

    @property
    def lib_type(self):
        return trans2list(self._cfg['libType'])

    @property
    def asm_type(self):
        return self._cfg['asmType']

    @staticmethod
    def _get_fea_and_inc(asm_fea):
        if '::' in asm_fea:
            return asm_fea.split('::')
        else:
            return asm_fea, ''

    def _asm_fea_check(self, asm_fea, asm_type, info):
        fea, inc = self._get_fea_and_inc(asm_fea)
        feas_info = self._features.feas_info
        if fea not in feas_info:
            raise ValueError("Unsupported '%s' in %s" % (fea, info))
        if asm_type not in feas_info[fea]['impl']:
            raise ValueError("Feature '%s' has no assembly implementation of type '%s' in %s" % (fea, asm_type, info))
        if inc:
            if inc not in feas_info[fea]['impl'] and inc not in feas_info[fea]['impl'][asm_type]:
                raise ValueError("Unsupported instruction set of '%s' in %s" % (asm_fea, info))
        return fea, inc

    def _file_check(self):
        for key, value in self.key_value.items():
            if value['require']:
                if key not in self._cfg.keys():
                    raise ValueError("Error feature_config file: missing '%s'" % key)

        for key, value in self._cfg.items():
            if key not in self.key_value.keys():
                raise ValueError("Error feature_config file: unsupported config '%s'" % key)
            if not isinstance(value, self.key_value.get(key).get("type")):
                raise ValueError("Error feature_config file: wrong type of '%s'" % key)

            value_type = type(value)
            if value_type == str or value_type == str:
                if value not in self.key_value.get(key).get("choices"):
                    raise ValueError("Error feature_config file: wrong value of '%s'" % key)
            elif value_type == list:
                choices = set(self.key_value[key]["choices"])
                if not set(value).issubset(choices):
                    raise ValueError("Error feature_config file: wrong value of '%s'" % key)

        for lib, lib_obj in self._cfg['libs'].items():
            if lib not in self._features.libs:
                raise ValueError("Error feature_config file: unsupported lib '%s'" % lib)
            for fea in lib_obj.get('c', []):
                if fea not in self._features.feas_info:
                    raise ValueError("Error feature_config file: unsupported fea '%s' in lib '%s'" % (fea, lib))
            asm_feas = []
            for asm_fea in lib_obj.get('asm', []):
                fea, inc = self._asm_fea_check(asm_fea, self.asm_type, 'feature_config file')
                if fea in asm_feas:
                    raise ValueError("Error feature_config file: duplicate assembly feature '%s'" % fea)
                asm_feas.append(fea)

    def set_param(self, key, value, set_default=True):
        if value:
            self._cfg[key] = value
            return
        if not set_default:
            return
        if key not in self._cfg or not self._cfg[key]:
            print("Warning: Configuration item '{}' is missing and has been set to the default value '{}'.".format(
                key, self.key_value.get(key).get('default')))
            self._cfg[key] = self.key_value.get(key).get('default')

    def _get_related_feas(self, fea, feas_info, related: set):
        related.add(fea)
        if 'parent' in feas_info[fea]:
            parent = feas_info[fea]['parent']
            for dep in feas_info[parent].get('deps', []):
                self._get_related_feas(dep, feas_info, related)
        if 'children' in feas_info[fea]:
            for child in feas_info[fea]['children']:
                self._get_related_feas(child, feas_info, related)
        if 'deps' in feas_info[fea]:
            for dep in feas_info[fea]['deps']:
                self._get_related_feas(dep, feas_info, related)

    def _get_json_feas(self, enable_feas, asm_feas):
        for _, lib_obj in self._cfg['libs'].items():
            for fea in lib_obj.get('c', []):
                related_feas = set()
                self._get_related_feas(fea, self._features.feas_info, related_feas)
                enable_feas.update(related_feas)
            for asm_fea in lib_obj.get('asm', []):
                fea, inc = self._get_fea_and_inc(asm_fea)
                enable_feas.add(fea)
                asm_feas[fea] = inc
                if 'children' in self._features.feas_info[fea]:
                    enable_feas.update(self._features.feas_info[fea]['children'])

    def _get_parents(self, disables):
        parents = set()
        for d in disables:
            relation = self._features.feas_info.get(d)
            if relation and 'parent' in relation:
                parents.add(relation['parent'])
        return parents

    def get_enable_feas(self, enables):
        """
        Obtain the enabled features from the configuration file and input parameters.
        enables: [IN] 'all': add full c features.
                      Excluding 'all': Incremental addition of C features
        """
        enable_feas = set()
        enable_asm_feas = {}
        self._get_json_feas(enable_feas, enable_asm_feas)
        if not enables:
            return enable_feas, enable_asm_feas
        # Obtains the properties from the input parameters.
        if 'all' in enables:
            enable_feas = set(self._features.feas_info.keys())
            return enable_feas, enable_asm_feas
        for enable in enables:
            if enable in self._features.libs:
                # lib
                for fea, info in self._features.feas_info.items():
                    if enable == info['lib']:
                        enable_feas.add(fea)
            else:
                # The feature is not lib and needs to be added separately.
                related_feas = set()
                self._get_related_feas(enable, self._features.feas_info, related_feas)
                enable_feas.update(related_feas)
        return enable_feas, enable_asm_feas

    def _add_feature(self, fea, impl_type, inc=''):
        add_fea = fea if inc == '' else '{}::{}'.format(fea, inc)
        lib = self._features.feas_info[fea]['lib']
        if lib not in self._cfg['libs']:
            self._cfg['libs'][lib] = {impl_type: [add_fea]}
        elif impl_type not in self._cfg['libs'][lib]:
            self._cfg['libs'][lib][impl_type] = [add_fea]
        elif fea not in self._cfg['libs'][lib][impl_type]:
            self._cfg['libs'][lib][impl_type].append(add_fea)

    def set_asm_type(self, asm_type):
        if self._cfg['asmType'] == 'no_asm':
            self._cfg['asmType'] = asm_type
        elif self._cfg['asmType'] != asm_type:
            raise ValueError('Error asmType: %s is different from feature_config file.' % (asm_type))

    def set_asm_features(self, enable_feas, added_asm_feas, asm_type, arg_asm):
        feas_info = self._features.feas_info
        # Clear the assembly features first.
        for lib in self._cfg['libs']:
            if 'asm' in self._cfg['libs'][lib]:
                self._cfg['libs'][lib]['asm'] = []
        # Add assembly features.
        if arg_asm:
            for asm_feature in arg_asm:
                fea, inc = self._asm_fea_check(asm_feature, asm_type, 'input asm list')
                if fea not in enable_feas:
                    raise ValueError("To add '%s' assembly requires add it to 'enable' list" % fea)
                if inc and inc != asm_type and inc not in self._features.feas_info[fea]['impl'][asm_type]:
                    raise ValueError("Unsupported instruction set '%s' of fea '%s'" % (inc, fea))
                self._add_feature(fea, 'asm', inc)
                added_asm_feas[fea] = inc
        else:
            for fea in enable_feas:
                if asm_type not in feas_info[fea]['impl']:
                    continue
                self._add_feature(fea, 'asm')
                added_asm_feas[fea] = ''

    def set_c_features(self, enable_feas):
        for fea in enable_feas:
            if 'c' in self._features.feas_info[fea]['impl']:
                self._add_feature(fea, 'c')

    def _update_enable_feature(self, features, disables):
        """
        The sub-feature macro is derived from the parent feature macro in the code.
        Therefore, the sub-feature is removed and the parent feature is retained.
        """
        disable_parents = self._get_parents(disables)
        tmp_feas = features.copy()
        enable_set = set()
        feas_info = self._features.feas_info
        for fea in tmp_feas:
            rel = feas_info[fea]
            if fea in disable_parents:
                if 'children' in rel:
                    enable_set.update(rel['children'])
                enable_set.discard(fea)
            else:
                is_fea_contained = False
                while 'parent' in rel:
                    if rel['parent'] in disables:
                        raise Exception("The 'disables' features {} and 'enables' featrues {} conflict".format(fea, disables))

                    if rel['parent'] in features:
                        is_fea_contained = True
                        break
                    rel = feas_info[rel['parent']]
                if not is_fea_contained:
                    enable_set.add(fea)
        enable_set.difference_update(set(disables))
        return list(enable_set)

    def _check_bn_config(self):
        lib = 'hitls_crypto'
        if lib not in self._cfg['libs']:
            return

        has_bn = False
        for impl_type in self._cfg['libs'][lib]:
            if 'bn' in self._cfg['libs'][lib][impl_type]:
                has_bn = True
                break

        if has_bn and 'bits' not in self._cfg:
            raise ValueError("If 'bn' is used, the 'bits' of the system must be configured.")

    def _check_system_config(self):
        lib = 'hitls_bsl'
        if lib not in self._cfg['libs']:
            return
        sys_feas = ['sal_mem', 'sal_thread', 'sal_lock', 'sal_time', 'sal_file', 'sal_net', 'sal_str']
        has_sys_feas = False
        for impl_type in self._cfg['libs'][lib]:
            for fea in self._cfg['libs'][lib][impl_type]:
                if fea in sys_feas:
                    has_sys_feas = True
                    break

        if has_sys_feas and 'system' not in self._cfg:
            raise ValueError("If %s is used, the system type must be configured." % sys_feas)

    def _re_sort_lib(self):
        # Change the key sequence of the 'libs' dictionary. Otherwise, the compilation fails.
        lib_sort = ['hitls_bsl', 'hitls_crypto', 'hitls_tls', "hitls_x509"]
        libs = self.libs.copy()
        self._cfg['libs'].clear()

        for lib in lib_sort:
            if lib in libs:
                self._cfg['libs'][lib] = libs[lib].copy()

    def update_feature(self, enables, disables):
        '''
        update feature:
        1. Add the default lib and features: hitls_bsl: sal
        2. Delete features based on the relationship between features.
        '''
        libs = self._cfg['libs']
        if len(libs) == 0:
            raise ValueError("No features are set, please check whether 'enable' and 'asm_type' need to be set")

        libs.setdefault('hitls_bsl', {'c':['sal']})
        if 'hitls_bsl' not in libs:
            libs['hitls_bsl'] = {'c':['sal']}
        elif 'c' not in libs['hitls_bsl']:
            libs['hitls_bsl']['c'] = ['sal']
        elif 'sal' not in libs['hitls_bsl']['c']:
            libs['hitls_bsl']['c'].append('sal')

        for lib in libs:
            if 'c' in libs[lib]:
                libs[lib]['c'] = self._update_enable_feature(libs[lib]['c'], disables)
                libs[lib]['c'].sort()
            if self.asm_type != 'no_asm' and self.asm_type in libs[lib]:
                libs[lib]['asm'] = self._update_enable_feature(libs[lib]['asm'], disables)
                libs[lib]['asm'].sort()

        self._re_sort_lib()

        if 'all' in enables:
            self.set_param('system', None)
            self.set_param('bits', None)
            return

    def save(self, path):
        save_json_file(self._cfg, path)

    def get_fea_macros(self):
        macros = set()
        for lib, lib_value in self.libs.items():
            lib_upper = lib.upper()

            for fea in lib_value.get('c', []):
                macros.add("-D%s_%s" % (lib_upper, fea.upper()))
            for fea in lib_value.get('asm', []):
                fea = fea.split('::')[0]
                macros.add("-D%s_%s" % (lib_upper, fea.upper()))
                macros.add("-D%s_%s_ASM" % (lib_upper, fea.upper()))
                macros.add("-D%s_%s_%s" % (lib_upper, fea.upper(), self.asm_type.upper()))

        if self._cfg['endian'] == 'big':
            macros.add("-DHITLS_BIG_ENDIAN")
        if self._cfg.get('system', ""):
            macros.add("-DHITLS_BSL_SAL_LINUX")

        bits = self._cfg.get('bits', 0)
        if bits == 32:
            macros.add("-DHITLS_THIRTY_TWO_BITS")
        elif bits == 64:
            macros.add("-DHITLS_SIXTY_FOUR_BITS")

        return list(macros)

    def _re_get_fea_modules(self, fea, feas_info, asm_type, inc, modules):
        """Obtain the modules on which the current feature and subfeature depend."""
        for mod in feas_info[fea].get('modules', []):
            modules.setdefault(mod, {})
            modules[mod]["asmType"] = asm_type
            if inc:
                modules[mod]["incSet"] = inc

        for child in feas_info[fea].get('children', []):
            self._re_get_fea_modules(child, feas_info, asm_type, inc, modules)

    def _get_lib_modules(self, lib):
        """Obtain the enabled modules and their instruction sets."""
        lib_modules = {}
        feas_info = self._features.feas_info
        for fea in self.libs[lib].get('c', []):
            self._re_get_fea_modules(fea, feas_info, 'c', '', lib_modules)

        for asm_fea in self.libs[lib].get('asm', []):
            fea, inc = self._get_fea_and_inc(asm_fea)
            self._re_get_fea_modules(fea, feas_info, self.asm_type, inc, lib_modules)

        return lib_modules

    def get_enable_modules(self):
        """
        Obtain the modules required for compiling each lib features
        and the modules on which the lib feature depends (for obtaining the include directory).
            1. Add modules and their dependent modules based on features.
            2. Check whether the dependent modules are enabled.
        return: {'lib':{"mod1":{"deps":[], "asmType":"", "incSet":""}}}
                Module format: top_dir::sub_dir
        """
        enable_libs_mods = {}
        enable_mods = set()
        for lib in self.libs.keys():
            enable_libs_mods[lib] = self._get_lib_modules(lib)
            for mod in enable_libs_mods[lib]:
                mod_dep_mods = set()
                self._features.get_module_deps(mod, [], mod_dep_mods)
                enable_libs_mods[lib][mod]['deps'] = list(mod_dep_mods)
            if len(enable_libs_mods[lib].keys()) == 0:
                raise ValueError("Error: no module is enabled in lib%s" % lib)
            enable_mods.update(enable_libs_mods[lib])

        # Check whether the dependent module is enabled.
        for lib in enable_libs_mods.keys():
            for mod in enable_libs_mods[lib]:
                for dep_mod in enable_libs_mods[lib][mod].get('deps', []):
                    if dep_mod == "platform::Secure_C":
                        continue
                    if dep_mod not in enable_mods:
                        raise ValueError("Error: '%s' depends on '%s', but '%s' is disabled." % (mod, dep_mod, dep_mod))
        return enable_libs_mods

    def filter_no_asm_config(self):
        self._cfg['asmType'] = 'no_asm'
        for lib in self._cfg['libs']:
            if 'asm' in self._cfg['libs'][lib]:
                self._cfg['libs'][lib]['asm'] = []

    def _check_fea_opts_arr(self, opts, fea, enable_feas):
        for opt_arr in opts:
            has_opt = False
            for opt_fea in opt_arr:
                parent = self._features.feas_info[opt_fea].get('parent', '')
                if opt_fea in enable_feas or (parent and parent in enable_feas):
                    has_opt = True
                    continue
            if not has_opt:
                raise ValueError("The fea '%s' must work with at leaset one fea in %s" % (fea, opt_arr))

    def _check_opts(self, fea, enable_feas):
        if 'opts' not in self._features.feas_info[fea]:
            return
        opts = self._features.feas_info[fea]['opts']
        if not isinstance(opts[0], list):
            opts = [opts]

        self._check_fea_opts_arr(opts, fea, enable_feas)

    def _check_family_opts(self, fea, key, enable_feas):
        values = self._features.feas_info[fea].get(key, [])
        if not isinstance(values, list):
            values = [values]
        for value in values:
            self._check_opts(value, enable_feas)
            self._check_family_opts(value, key, enable_feas)

    def check_fea_opts(self):
        enable_feas = set()
        for _, lib_obj in self.libs.items():
            enable_feas.update(lib_obj.get('c', []))
            enable_feas.update(lib_obj.get('asm', []))
        for fea in enable_feas:
            fea = fea.split("::")[0]
            self._check_opts(fea, enable_feas)
            self._check_family_opts(fea, 'parent', enable_feas)
            self._check_family_opts(fea, 'children', enable_feas)

class CompleteOptionParser:
    """ Parses all compilation options. """
    # Sequence in which compilation options are added, including all compilation option types.
    option_order = [
        "CC_DEBUG_FLAGS",
        "CC_OPT_LEVEL",  # Optimization Level
        "CC_OVERALL_FLAGS",  # Overall Options
        "CC_WARN_FLAGS",  # Warning options
        "CC_LANGUAGE_FLAGS",  # Language Options
        "CC_CDG_FLAGS",  # Code Generation Options
        "CC_MD_DEPENDENT_FLAGS",  # MD_Dependent Options
        "CC_OPT_FLAGS",  # Optimization Options
        "CC_SEC_FLAGS",  # Secure compilation options
        "CC_DEFINE_FLAGS",  # Custom Macro
        "CC_USER_DEFINE_FLAGS", # User-defined compilation options are reserved.
    ]

    def __init__(self, file_path):
        self._fp = file_path
        with open(file_path, 'r') as f:
            self._cfg = json.loads(f.read())
            self._file_check()

        self._option_type_map = {}
        for option_type in self._cfg['compileFlag']:
            for option in trans2list(self._cfg['compileFlag'][option_type]):
                self._option_type_map[option] = option_type

    @property
    def option_type_map(self):
        return self._option_type_map

    @property
    def type_options_map(self):
        return self._cfg['compileFlag']

    def _file_check(self):
        if 'compileFlag' not in self._cfg or 'linkFlag' not in self._cfg:
            raise FileNotFoundError("The format of file %s is incorrect." % self._fp)
        for option_type in self._cfg['compileFlag']:
            if option_type not in self.option_order:
                raise FileNotFoundError("The format of file %s is incorrect." % self._fp)

class CompileConfigParser:
    """ Parse the user compilation configuration file. """

    def __init__(self, all_options: CompleteOptionParser, file_path=''):
        with open(file_path, 'r') as f:
            self._cfg = json.loads(f.read())
        self._all_options = all_options
        self._file_check()

    @property
    def options(self):
        return self._cfg['compileFlag']

    @property
    def link_flags(self):
        return self._cfg['linkFlag']

    @classmethod
    def default_cfg(cls):
        config = {
            'compileFlag': {},
            'linkFlag': {}
        }
        return config

    def _file_check(self):
        if 'compileFlag' not in self._cfg or 'linkFlag' not in self._cfg:
            raise FileNotFoundError("Error compile_config file: missing 'compileFlag' or 'linkFlag'")

        # Check whether the configured compilation options are in the compilation option set.
        for option_type in self._cfg['compileFlag']:
            if option_type == 'CC_USER_DEFINE_FLAGS':
                continue

            for option in self._cfg['compileFlag'][option_type].get('CC_FLAGS_ADD', []):
                if option not in self._all_options.type_options_map[option_type]:
                    raise ValueError("unrecognized option {}".format(option))

    def save(self, path):
        save_json_file(self._cfg, path)

    def change_options(self, options, is_add):
        option_op = 'CC_FLAGS_ADD' if is_add else 'CC_FLAGS_DEL'
        for option in options:
            option_type = 'CC_USER_DEFINE_FLAGS'
            if option in self._all_options.option_type_map:
                option_type = self._all_options.option_type_map[option]

            if option_type not in self._cfg['compileFlag']:
                self._cfg['compileFlag'][option_type] = {}

            flags = self._cfg['compileFlag'][option_type]
            flags[option_op] = unique_list(flags.get(option_op, []) + [option])

    def change_link_flags(self, flags, is_add):
        link_op = 'LINK_FLAG_ADD' if is_add else 'LINK_FLAG_DEL'
        new_flags = self._cfg['linkFlag'].get(link_op, []) + flags
        self._cfg['linkFlag'][link_op] = unique_list(new_flags)

    def add_debug_options(self):
        flags_add = {'CC_FLAGS_ADD': ['-g3', '-gdwarf-2']}
        flags_del = {'CC_FLAGS_DEL': ['-O2', '-D_FORTIFY_SOURCE=2']}

        self._cfg['compileFlag']['CC_DEBUG_FLAGS'] = flags_add
        self._cfg['compileFlag']['CC_OPT_LEVEL'] = flags_del

    def filter_hitls_defines(self):
        for flag in list(self.link_flags.keys()):
            del self.link_flags[flag]

        for flag in list(self.options.keys()):
            if flag != 'CC_USER_DEFINE_FLAGS' and flag != 'CC_DEFINE_FLAGS':
                del self.options[flag]

class CompileParser:
    """
    Parse the compile.json file.
    json key and value:
        compileFlag: compilation options
        linkFlag: link option
    """

    def __init__(self, all_options: CompleteOptionParser, file_path):
        self._fp = file_path
        self._all_options = all_options
        with open(file_path, 'r') as f:
            self._cfg = json.loads(f.read())
        self._file_check()

    @property
    def options(self):
        return self._cfg["compileFlag"]

    @property
    def link_flags(self):
        return self._cfg["linkFlag"]

    def _file_check(self):
        if 'compileFlag' not in self._cfg or 'linkFlag' not in self._cfg:
            raise FileNotFoundError("Error compile file: missing 'compileFlag' or 'linkFlag'")
        for option_type in self.options:
            if option_type not in self._all_options.type_options_map:
                raise ValueError("no '{}' option type in complete_options.json".format(option_type))

            for option in self.options[option_type]:
                if option not in self._all_options.type_options_map[option_type]:
                    raise ValueError("unrecognized option '{}' in type {}.".format(option, option_type))
        for option_type in self._cfg['linkFlag']:
            if option_type not in ['PUBLIC', 'SHARED', 'EXE']:
                raise FileNotFoundError('Incorrect file format: %s' % self._fp)

    def union_options(self, custom_cfg: CompileConfigParser):
        options = []
        for option_type in CompleteOptionParser.option_order:
            options.extend(self.options.get(option_type, []))
            if option_type not in custom_cfg.options:
                continue
            for option in custom_cfg.options[option_type].get('CC_FLAGS_ADD', []):
                if option not in options:
                    options.append(option)
            for option in custom_cfg.options[option_type].get('CC_FLAGS_DEL', []):
                if option in options:
                    options.remove(option)

        flags = self.link_flags
        for flag in custom_cfg.link_flags.get('LINK_FLAG_ADD', []):
            if flag not in flags['PUBLIC']:
                flags['PUBLIC'].append(flag)
            if flag not in flags['EXE']:
                flags['EXE'].append(flag)
            if flag not in flags['SHARED']:
                flags['SHARED'].append(flag)
        for flag in custom_cfg.link_flags.get('LINK_FLAG_DEL', []):
            if flag in flags['PUBLIC']:
                flags['PUBLIC'].remove(flag)
            if flag in flags['EXE']:
                flags['EXE'].remove(flag)
            if flag in flags['SHARED']:
                flags['SHARED'].remove(flag)

        return options, flags