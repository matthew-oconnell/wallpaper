# GUI Design
The application GUI should look like this with 4 key sections:

 - General settings section
 - Thumbnail section
 - Selected thumbnail section
 - Apply / button section

------------------------------------------
|                                        |
| here show general settings.            |
|    How often to change wallpaper       |
|    what subreddits to pull from (list) |
|    positioning (scale, crop, centered) |
|                                        |
|----------------------------------------|
|                                        |
|                                        |
|                                        |
|                                        |
|   here show thumnails of the images    |
|            in the cache                |
|                                        |
|                                        |
|                                        |
|                                        |
|                                        |
|----------------------------------------|
| for a selected thumbnail show stats:   |
|  - what subreddit it came from         |
|  - resolution                          |
|  - thumbs up / down counter            |
|  - perma-ban                           |
|----------------------------------------|
|                      Button: Randomize |
------------------------------------------

# Tray design
From the tray we should be able to:
- thumbs up / down/ permaban the current wallpaper
- randomize the wallpaper
- open the full window
- close the application

# User rating: 
For each image we should remember what subreddit it came from and allow the user to thumbs up and thumbs down it. Every time the user thumbs up the wallpaper it should be more likely to be shown in the future.  Every time it's thumbs downed it should be less likely. 

# Randomize wallpaper on timer
The application should randomize the wallpaper every N seconds, minutes or hours.  This should be configurable in the application's general settings section. 

# Maintain multiple subreddit sources
We should be able to add multiple subreddits as a source for wallpapers and manage that list from the general sections settings. 