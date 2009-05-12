"""Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=21034
"""

import dbus
import dbus.service

from servicetest import EventPattern, tp_name_prefix, tp_path_prefix, \
        call_async
from mctest import exec_test, SimulatedConnection, SimulatedClient, \
        create_fakecm_account, enable_fakecm_account, SimulatedChannel, \
        expect_client_setup
import constants as cs

def test(q, bus, mc):
    params = dbus.Dictionary({"account": "someguy@example.com",
        "password": "secrecy"}, signature='sv')
    cm_name_ref, account = create_fakecm_account(q, bus, mc, params)
    conn = enable_fakecm_account(q, bus, mc, account, params)

    text_fixed_properties = dbus.Dictionary({
        cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
        cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
        }, signature='sv')

    client = SimulatedClient(q, bus, 'Empathy',
            observe=[text_fixed_properties], approve=[text_fixed_properties],
            handle=[text_fixed_properties], bypass_approval=False)

    # wait for MC to download the properties
    expect_client_setup(q, [client])

    cd = bus.get_object(cs.CD, cs.CD_PATH)
    cd_props = dbus.Interface(cd, cs.PROPERTIES_IFACE)

    # chat UI calls ChannelDispatcher.EnsureChannel or CreateChannel
    request = dbus.Dictionary({
            cs.CHANNEL + '.ChannelType': cs.CHANNEL_TYPE_TEXT,
            cs.CHANNEL + '.TargetHandleType': cs.HT_CONTACT,
            cs.CHANNEL + '.TargetID': 'juliet',
            }, signature='sv')
    account_requests = dbus.Interface(account,
            cs.ACCOUNT_IFACE_NOKIA_REQUESTS)

    call_async(q, cd, 'CreateChannel',
            account.object_path, request, dbus.Int64(1234),
            'grr.arg',      # a valid bus name, but the wrong prefix
            dbus_interface=cs.CD)
    ret = q.expect('dbus-error', method='CreateChannel')
    assert ret.error.get_dbus_name() == cs.INVALID_ARGUMENT

    call_async(q, cd, 'CreateChannel',
            account.object_path, request, dbus.Int64(1234),
            'can has cheeseburger?',      # a totally invalid bus name
            dbus_interface=cs.CD)
    ret = q.expect('dbus-error', method='CreateChannel')
    assert ret.error.get_dbus_name() == cs.INVALID_ARGUMENT

if __name__ == '__main__':
    exec_test(test, {})
