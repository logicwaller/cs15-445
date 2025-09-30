/* 获取运动员加团体的code-城市code对应表 */
with code_country as(
    select code, country_code from athletes
    union
    select code, country_code from teams
),
/* 获取获奖的(discipline,country_code)数量 */
medal_country as(
    select discipline, country_code, count(1) as number 
    from code_country, medals
    where medals.winner_code = code_country.code
    group by discipline, country_code
)

select name as coach_name, number as medal_number
from coaches, medal_country
where coaches.country_code = medal_country.country_code
    and coaches.discipline = medal_country.discipline
order by medal_number desc, coach_name