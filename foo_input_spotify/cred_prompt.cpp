#include "util.h"
#include "cred_prompt.h"

#include <windows.h>
#include <WinCred.h>
#include <Ntsecapi.h>

std::vector<WCHAR> previousUsername;

std::auto_ptr<CredPromptResult> credPrompt(pfc::string8 msg) {
	std::auto_ptr<CredPromptResult> cpr(new CredPromptResult());

	CREDUI_INFOW cui;
	WCHAR pszName[CREDUI_MAX_USERNAME_LENGTH + 1];
	WCHAR pszPwd[CREDUI_MAX_PASSWORD_LENGTH + 1];
	BOOL fSave;
	DWORD dwErr;

	cui.cbSize = sizeof(CREDUI_INFO);
	cui.hwndParent = NULL;
	//  Ensure that MessageText and CaptionText identify what credentials
	//  to use and which application requires them.
	cui.pszMessageText = L"Enter your Spotify credentials";
	cui.pszCaptionText = L"Spotify";
	cui.hbmBanner = NULL;
	fSave = FALSE;
	SecureZeroMemory(pszName, sizeof(pszName));
	SecureZeroMemory(pszPwd, sizeof(pszPwd));
	dwErr = CredUIPromptForCredentialsW(
		&cui,                         // CREDUI_INFO structure
		TEXT("foo_input_spotify"),            // Target for credentials
		NULL,                         // Reserved
		0,                            // Reason
		pszName,                      // User name
		CREDUI_MAX_USERNAME_LENGTH + 1, // Max number of char for user name
		pszPwd,                       // Password
		CREDUI_MAX_PASSWORD_LENGTH + 1, // Max number of char for password
		&fSave,                       // State of save check box
		CREDUI_FLAGS_GENERIC_CREDENTIALS |  // flags
		CREDUI_FLAGS_ALWAYS_SHOW_UI |
		CREDUI_FLAGS_DO_NOT_PERSIST);

	if (!dwErr)
	{

		pfc::stringcvt::convert_wide_to_utf8(cpr->un.data(), CRED_BUF_SIZE, pszName, sizeof(pszName));
		pfc::stringcvt::convert_wide_to_utf8(cpr->pw.data(), CRED_BUF_SIZE, pszPwd, sizeof(pszPwd));
		cpr->save = true;

		SecureZeroMemory(pszName, sizeof(pszName));
		SecureZeroMemory(pszPwd, sizeof(pszPwd));
	}
	return cpr;
}