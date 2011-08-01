from Plugins.Plugin import PluginDescriptor
from Components.config import config, ConfigSubsection, ConfigInteger, ConfigSelection, ConfigText, ConfigEnableDisable, ConfigYesNo

from vTunerConfig import configCB, vTunerConfigScreen
import pydevd
    
def openconfig(session, **kwargs):
    pydevd.settrace( host="192.168.1.100", stdoutToServer=True, stderrToServer=True )
    session.openWithCallback(configCB, vTunerConfigScreen)

def Plugins(**kwargs):
    return [
#        PluginDescriptor(
#            name="vTuner",
#            description="vTuner resource management and discover",
#            where = PluginDescriptor.WHERE_AUTOSTART,
#            fnc=autostart
#        ),
        PluginDescriptor(
             name="vTuner", 
             description="vTuner Configuration",
             where = PluginDescriptor.WHERE_PLUGINMENU,
             fnc=openconfig 
         )
    ]
