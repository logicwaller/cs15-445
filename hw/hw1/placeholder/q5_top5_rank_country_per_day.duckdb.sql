with participant_country as(
    select code, country_code from athletes
    union
    select code, country_code from teams
),
top5_appearence as(
    select 
        date, 
        country_code, 
        count(1) as appear,
        rank() over (partition by date order by appear desc) as arank
    from results, participant_country as p
    where results.participant_code = p.code
        and results.rank <= 5
    group by date, country_code
)

select date, country_code, appear as top5_appearances, gdp_rank, population_rank
from 
    (select * from top5_appearence where arank = 1) as p
    left join
    (select 
        code,
        rank() over (order by "GDP ($ per capita)" desc) as gdp_rank,
        rank() over (order by population desc) as population_rank
    from countries) as c
    on p.country_code = c.code
order by date;