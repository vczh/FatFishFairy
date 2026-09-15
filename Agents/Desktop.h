#ifndef FATFISH_DESKTOP_H
#define FATFISH_DESKTOP_H

#include <VlppOS.h>
#include <random>

namespace fatfish
{
	struct DesktopPosition
	{
		vl::vint                                        x = 0;
		vl::vint                                        y = 0;
	};

	// The UI supplies the exact environment folder. Missing coordinates default to zero.
	// Coordinates must fit the signed 32-bit Windows desktop coordinate range.
	extern DesktopPosition LoadDesktopPosition(const vl::filesystem::FilePath& envFolder);
	extern void SaveDesktopPosition(const vl::filesystem::FilePath& envFolder, DesktopPosition position);

	class ThemeAnimation : public vl::Object
	{
	public:
		vl::WString                                     name;
		vl::collections::List<vl::filesystem::FilePath>   frames;
	};

	class DesktopTheme : public vl::Object
	{
	public:
		vl::WString                                     name;
		vl::WString                                     displayName;
		vl::collections::List<vl::Ptr<ThemeAnimation>>    animations;
	};

	// Theme and animation IDs contain only ASCII letters, digits, underscores and hyphens.
	// Preserve metadata order; all declared frames must exist before replacing the output.
	extern void LoadDesktopThemes(const vl::filesystem::FilePath& themesFolder, vl::collections::List<vl::Ptr<DesktopTheme>>& themes);
	// Select an exact catalog key; missing and unknown keys fall back to the first theme.
	extern vl::vint LoadSelectedDesktopTheme(const vl::filesystem::FilePath& envFolder, const vl::collections::List<vl::Ptr<DesktopTheme>>& themes);
	extern void SaveSelectedDesktopTheme(const vl::filesystem::FilePath& envFolder, const vl::WString& themeName);

	class ThemePlayback
	{
	private:
		vl::Ptr<DesktopTheme>                           theme;
		std::mt19937_64                                 random;
		vl::vint                                        animationIndex = 0;
		vl::vint                                        frameIndex = 0;
		vl::vint                                        completedPlaythroughs = 0;

		void                                            SelectAnimation();

	public:
		// Supply a fresh seed on each process start. Do not mutate themes while playing them.
		                                                ThemePlayback(vl::Ptr<DesktopTheme> desktopTheme, vl::vuint64_t seed);
		// Immediately begin a random series at frame one, retaining the player's random state.
		void                                            SetTheme(vl::Ptr<DesktopTheme> desktopTheme);
		const vl::filesystem::FilePath&                  CurrentFrame() const;
		const vl::WString&                              CurrentAnimation() const;
		// Call once per second after displaying CurrentFrame(). Play every series three times.
		void                                            Advance();
	};
}

#endif
