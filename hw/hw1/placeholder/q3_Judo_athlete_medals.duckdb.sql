/* 记录judo运动员的团体 */
with judo_aths as(
    select aths.code as ath_code, name, teams.code as team_code
    from (select * from athletes where disciplines like '%Judo%') as aths
    left join teams 
    on aths.code = teams.athletes_code
),
/* 记录得奖的运动员的得奖数 */
judo_aths_medal as(
    select ath_code, count(1) as medal_number
    from judo_aths, medals
        where judo_aths.ath_code = medals.winner_code
            or judo_aths.team_code = medals.winner_code
    group by ath_code
)

select name as athlete_name, 
        case when medal_number is null
        then 0
        else medal_number end as medals_number
from judo_aths left join judo_aths_medal 
    on judo_aths.ath_code = judo_aths_medal.ath_code
order by medal_number desc, athlete_name;