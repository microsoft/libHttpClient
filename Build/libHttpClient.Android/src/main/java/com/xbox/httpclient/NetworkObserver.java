package com.xbox.httpclient;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.util.LinkedHashMap;
import java.util.Map;

public class NetworkObserver {
    @NotNull private static String s_lastCapabilities = "";
    @NotNull private static String s_lastLinkProperties = "";

    private static ConnectivityManager.NetworkCallback s_networkChangedCallback = new ConnectivityManager.NetworkCallback() {
        @Override
        public void onAvailable(Network network) {
            LogMessage(network, "is available");
        }

        @Override
        public void onLost(Network network) {
            LogMessage(network, "was lost");
        }

        @Override
        public void onUnavailable() {
            Log("No networks were available");
        }

        @Override
        public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) {
            String newCapabilities = NetworkDetails.checkNetworkCapabilities(capabilities);

            if (!newCapabilities.equals(s_lastCapabilities)) {
                s_lastCapabilities = newCapabilities;
                LogMessage(network, "has capabilities: " + s_lastCapabilities);
            }
        }

        @Override
        public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {
            String newLinkProperties = NetworkDetails.checkLinkProperties(linkProperties);

            if (!newLinkProperties.equals(s_lastLinkProperties)) {
                s_lastLinkProperties = newLinkProperties;
                LogMessage(network, "has link properties: " + s_lastLinkProperties);
            }
        }

        private void LogMessage(Network network, String message) {
            Log("Network ID " + network.hashCode() + " " + message);
        }
    };

    @SuppressWarnings("unused")
    public static void Initialize(Context appContext) {
        ConnectivityManager cm = (ConnectivityManager)appContext.getSystemService(Context.CONNECTIVITY_SERVICE);

        // Build a network request to query for all networks that "should" be
        // able to access the internet
        NetworkRequest networkRequest = new NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build();

        cm.registerNetworkCallback(networkRequest, s_networkChangedCallback);
    }

    @SuppressWarnings("unused")
    public static void Cleanup(Context appContext) {
        ConnectivityManager cm = (ConnectivityManager)appContext.getSystemService(Context.CONNECTIVITY_SERVICE);

        cm.unregisterNetworkCallback(s_networkChangedCallback);
    }

    private static native void Log(String message);

    static class NetworkDetails {
        static String getNetworkDetails(Network network, ConnectivityManager cm) {
            StringBuilder builder = new StringBuilder();

            @Nullable NetworkCapabilities capabilities = cm.getNetworkCapabilities(network);
            @Nullable LinkProperties linkProperties = cm.getLinkProperties(network);

            return builder
                .append("Network ")
                .append(network.hashCode())
                .append(":")
                .append("\n  Capabilities: ")
                .append(capabilities != null ? checkNetworkCapabilities(capabilities) : "Got null capabilities")
                .append("\n  Link properties: ")
                .append(linkProperties != null ? checkLinkProperties(linkProperties) : "Got null link properties")
                .toString();
        }

        private static String checkNetworkCapabilities(@NotNull NetworkCapabilities capabilities) {
            Map<String, Boolean> sections = new LinkedHashMap<>();

            sections.put("isWifi", capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI));
            sections.put("isBluetooth", capabilities.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH));
            sections.put("isCellular", capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR));
            sections.put("isVpn", capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN));
            sections.put("isEthernet", capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));

            sections.put("shouldHaveInternet", capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET));
            sections.put("isNotVpn", capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN));

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                sections.put("internetWasValidated", capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED));
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                sections.put("isNotSuspended", capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED));
            }

            return join(sections);
        }

        private static String checkLinkProperties(@NotNull LinkProperties properties) {
            Map<String, Boolean> sections = new LinkedHashMap<>();

            sections.put("hasProxy", properties.getHttpProxy() != null);

            return join(sections);
        }

        private static String join(Map<String, Boolean> sections) {
            StringBuilder builder = new StringBuilder();

            for (Map.Entry<String, Boolean> entry : sections.entrySet()) {
                if (builder.length() > 0)  {
                    builder.append(", ");
                }

                builder
                    .append(entry.getKey())
                    .append(": ")
                    .append(entry.getValue());
            }

            return builder.toString();
        }
    }
}

