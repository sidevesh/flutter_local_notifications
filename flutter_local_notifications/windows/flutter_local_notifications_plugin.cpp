#include "include/flutter_local_notifications/flutter_local_notifications_plugin.h"
#include "include/flutter_local_notifications/methods.h"
#include "utils/utils.h"
#include "registration.h"

// This must be included before many other Windows headers.
#include <windows.h>
#include <ShObjIdl_core.h>
#include <NotificationActivationCallback.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.Data.Xml.Dom.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <memory>
#include <sstream>

using namespace winrt::Windows::Data::Xml::Dom;

// static
void FlutterLocalNotificationsPlugin::RegisterWithRegistrar(
	flutter::PluginRegistrarWindows* registrar) {
	auto channel =
		std::make_unique<PluginMethodChannel>(
			registrar->messenger(), "dexterous.com/flutter/local_notifications",
			&flutter::StandardMethodCodec::GetInstance());

	auto plugin = std::make_unique<FlutterLocalNotificationsPlugin>(*channel);

	channel->SetMethodCallHandler(
		[plugin_pointer = plugin.get()](const auto& call, auto result) {
		plugin_pointer->HandleMethodCall(call, std::move(result));
	});

	registrar->AddPlugin(std::move(plugin));
}

FlutterLocalNotificationsPlugin::FlutterLocalNotificationsPlugin(PluginMethodChannel& channel) :
	channel(channel) {}

FlutterLocalNotificationsPlugin::~FlutterLocalNotificationsPlugin() {}

PluginMethodChannel& FlutterLocalNotificationsPlugin::GetPluginMethodChannel() {
	return channel;
}

void FlutterLocalNotificationsPlugin::HandleMethodCall(
	const flutter::MethodCall<flutter::EncodableValue>& method_call,
	std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result
) {
	const auto& method_name = method_call.method_name();
	if (method_name == Method::GET_NOTIFICATION_APP_LAUNCH_DETAILS) {
		result->Success();
	}
	else if (method_name == Method::INITIALIZE) {
		const auto args = std::get_if<flutter::EncodableMap>(method_call.arguments());
		if (args != nullptr) {
			const auto appName = Utils::GetMapValue<std::string>("appName", args).value();
			const auto aumid = Utils::GetMapValue<std::string>("aumid", args).value();
			const auto iconPath = Utils::GetMapValue<std::string>("iconPath", args);
			const auto iconBgColor = Utils::GetMapValue<std::string>("iconBgColor", args);

			Initialize(appName, aumid, iconPath, iconBgColor);
			result->Success(true);
		}
		else {
			result->Error("INTERNAL", "flutter_local_notifications encountered an internal error.");
		}
	}
	else if (method_name == Method::SHOW) {
		channel.InvokeMethod("test", nullptr, nullptr);

		const auto args = std::get_if<flutter::EncodableMap>(method_call.arguments());
		if (args != nullptr && toastNotifier.has_value()) {
			const auto id = Utils::GetMapValue<int>("id", args).value();
			const auto title = Utils::GetMapValue<std::string>("title", args);
			const auto body = Utils::GetMapValue<std::string>("body", args);
			const auto payload = Utils::GetMapValue<std::string>("payload", args);
			const auto group = Utils::GetMapValue<std::string>("group", args);

			ShowNotification(id, title, body, payload, group);
			result->Success();
		}
		else {
			result->Error("INTERNAL", "flutter_local_notifications encountered an internal error.");
		}
	}
	else if (method_name == Method::CANCEL && toastNotifier.has_value()) {
		const auto args = std::get_if<flutter::EncodableMap>(method_call.arguments());
		if (args != nullptr) {
			const auto id = Utils::GetMapValue<int>("id", args).value();
			const auto group = Utils::GetMapValue<std::string>("group", args);

			CancelNotification(id, group);
			result->Success();
		}
		else {
			result->Error("INTERNAL", "flutter_local_notifications encountered an internal error.");
		}
	}
	else if (method_name == Method::CANCEL_ALL && toastNotifier.has_value()) {
		CancelAllNotifications();
		result->Success();
	}
	else {
		result->NotImplemented();
	}
}

void FlutterLocalNotificationsPlugin::Initialize(
	const std::string& appName,
	const std::string& aumid,
	const std::optional<std::string>& iconPath,
	const std::optional<std::string>& iconBgColor
) {
	_aumid = winrt::to_hstring(aumid);
	PluginRegistration::RegisterApp(aumid, appName, iconPath, iconBgColor, this);
	toastNotifier = winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(winrt::to_hstring(aumid));
}

void FlutterLocalNotificationsPlugin::ShowNotification(
	const int id,
	const std::optional<std::string>& title,
	const std::optional<std::string>& body,
	const std::optional<std::string>& payload,
	const std::optional<std::string>& group
) {
	// obtain a notification template with a title and a body
	const auto doc = winrt::Windows::UI::Notifications::ToastNotificationManager::GetTemplateContent(winrt::Windows::UI::Notifications::ToastTemplateType::ToastText02);
	// find all <text /> tags
	const auto nodes = doc.GetElementsByTagName(L"text");

	if (title.has_value()) {
		// change the text of the first <text></text>, which will be the title
		nodes.Item(0).AppendChild(doc.CreateTextNode(winrt::to_hstring(title.value())));
	}
	if (body.has_value()) {
		// change the text of the second <text></text>, which will be the body
		nodes.Item(1).AppendChild(doc.CreateTextNode(winrt::to_hstring(body.value())));
	}

	winrt::Windows::UI::Notifications::ToastNotification notif{ doc };
	notif.Tag(winrt::to_hstring(id));
	if (group.has_value()) {
		notif.Group(winrt::to_hstring(group.value()));
	}
	else {
		notif.Group(_aumid);
	}

	toastNotifier.value().Show(notif);
}

void FlutterLocalNotificationsPlugin::CancelNotification(const int id, const std::optional<std::string>& group) {
	if (!toastNotificationHistory.has_value()) {
		toastNotificationHistory = winrt::Windows::UI::Notifications::ToastNotificationManager::History();
	}

	if (group.has_value()) {
		toastNotificationHistory.value().Remove(winrt::to_hstring(id), winrt::to_hstring(group.value()), _aumid);
	}
	else {
		toastNotificationHistory.value().Remove(winrt::to_hstring(id), _aumid, _aumid);
	}
}

void FlutterLocalNotificationsPlugin::CancelAllNotifications() {
	if (!toastNotificationHistory.has_value()) {
		toastNotificationHistory = winrt::Windows::UI::Notifications::ToastNotificationManager::History();
	}
	toastNotificationHistory.value().Clear(_aumid);
}

void FlutterLocalNotificationsPluginRegisterWithRegistrar(
	FlutterDesktopPluginRegistrarRef registrar) {
	FlutterLocalNotificationsPlugin::RegisterWithRegistrar(
		flutter::PluginRegistrarManager::GetInstance()
		->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
