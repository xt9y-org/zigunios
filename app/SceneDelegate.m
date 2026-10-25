#import <UIKit/UIKit.h>

@interface TerminalViewController : UIViewController
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation SceneDelegate

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)connectionOptions
{
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[TerminalViewController alloc] init];
    [self.window makeKeyAndVisible];
}

@end
