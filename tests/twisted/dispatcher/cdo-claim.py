 # Copyright (C) 2009-2011 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

"""Test ChannelDispatchOperation.Claim(). See fd.o#40283
"""

import dbus
import dbus.service

from servicetest import call_async, assertEquals, EventPattern
from mctest import exec_test, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    # Logger is a text observer wanting recovery
    logger = SimulatedClient(q, bus, 'LoggerChat',
            observe=[text_fixed_properties],
            bypass_approval=False,
            wants_recovery=True)

    # gnome-shell is a text approver and handler
    gs = SimulatedClient(q, bus, 'GnomeShell',
            approve=[text_fixed_properties],
            handle=[text_fixed_properties],
            bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [logger, gs])

    cd = bus.get_object(cs.CD, cs.CD_PATH)

    # incoming text channel
    channel_properties = dbus.Dictionary(text_fixed_properties,
            signature='sv')
    channel_properties[cs.CHANNEL + '.TargetID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.TargetHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.InitiatorID'] = 'juliet'
    channel_properties[cs.CHANNEL + '.InitiatorHandle'] = \
            conn.ensure_handle(cs.HT_CONTACT, 'juliet')
    channel_properties[cs.CHANNEL + '.Requested'] = False
    channel_properties[cs.CHANNEL + '.Interfaces'] = dbus.Array(signature='s')

    chan = SimulatedChannel(conn, channel_properties)
    chan.announce()

    # gnome-shell's approver and logger's observer are notified
    e, k = q.expect_many(
            EventPattern('dbus-method-call',
                path=logger.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False),
            EventPattern('dbus-method-call',
                path=gs.object_path,
                interface=cs.APPROVER, method='AddDispatchOperation',
                handled=False),
            )

    channels, cdo_path, props = k.args

    cdo = bus.get_object(cs.CD, cdo_path)
    cdo_iface = dbus.Interface(cdo, cs.CDO)

    q.dbus_return(e.message, signature='')
    q.dbus_return(k.message, signature='')

    # gnome-shell claims the channel
    call_async(q, cdo_iface, 'Claim')
    e = q.expect('dbus-return', method='Claim')

    # Logger crash
    logger.release_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == logger.bus_name and e.args[2] == ''),
            )

    bus.flush()

    # Logger gets restarted
    logger.reacquire_name()

    e = q.expect('dbus-signal',
            signal='NameOwnerChanged',
            predicate=(lambda e:
                e.args[0] == logger.bus_name and e.args[1] == ''),
            )

    # Logger recovers the channel
    e = q.expect('dbus-method-call',
                path=logger.object_path,
                interface=cs.OBSERVER, method='ObserveChannels',
                handled=False)

    # gnome-shell which is handling the channel asks to re-ensure it
    cd_iface = dbus.Interface(cd, cs.CD)
    call_async(q, cd_iface, 'PresentChannel',
        chan.object_path, 0)

    # gnome-shell is asked to re-handle the channel
    e = q.expect('dbus-method-call',
            path=gs.object_path,
            interface=cs.HANDLER, method='HandleChannels',
            handled=False)

    q.dbus_return(e.message, signature='')

    q.expect('dbus-return', method='PresentChannel')

    chan.close()

if __name__ == '__main__':
    exec_test(test, {})
