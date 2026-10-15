#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

/* Only a bundled app has a notification center; a bare program answers 0. */
int
reaktor_notify_macos(const char *title, const char *text)
{
    UNUserNotificationCenter     *center;
    UNMutableNotificationContent *content;
    UNNotificationRequest        *request;

    if (![[NSBundle mainBundle] bundleIdentifier]) return 0;
    center  = [UNUserNotificationCenter currentNotificationCenter];
    content = [[UNMutableNotificationContent alloc] init];
    content.title = [NSString stringWithUTF8String:title];
    content.body  = [NSString stringWithUTF8String:text];
    request = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                   content:content
                                                   trigger:nil];
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                          completionHandler:^(BOOL granted, NSError *error) {
        (void)error;
        if (granted) [center addNotificationRequest:request withCompletionHandler:nil];
    }];
    return 1;
}
